/*
 * ============================================================================
 * ESP32 CYD (ESP32-2432S028R) AI COMPANION v1.1
 * On-Device Offline LLM (llama2.c with stories260k in Flash)
 *
 * Updates:
 *   - Fixed CYD Touch mapping (X/Y axis swap & quadrant hit detection)
 *   - Extended story length (seq_len = 192) for complete, full storylines
 *   - Exact BPE prompt tokens for all 4 buttons:
 *       [STORY]  -> "Once upon a time..." (Classic fairy tale)
 *       [SPARK]  -> "One day, Lily..." (Playful adventure)
 *       [PET]    -> "Tim and his dog..." (Loyal animal friend)
 *       [RANDOM] -> "There was a..." (Surprise randomized story)
 *   - 1.5s reading pause when terminal page fills before advancing
 *   - Displays "[THE END] ✨" upon story completion
 * ============================================================================
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include "model_data.h"

// ----------------------------------------------------------------------------
// CYD Hardware Pin Definitions (ESP32-2432S028R)
// ----------------------------------------------------------------------------
#define CYD_LED_RED     4
#define CYD_LED_RED_ALT 22
#define CYD_LED_GREEN   16
#define CYD_LED_BLUE    17

// CYD Touch Pins (XPT2046 SPI)
#define XPT_CLK         25
#define XPT_CS          33
#define XPT_MOSI        32
#define XPT_MISO        39
#define XPT_IRQ         36

// CYD Backlight Pin
#define CYD_TFT_BL      21

// ----------------------------------------------------------------------------
// Color Palette (Cyberpunk / Neon Dark Mode)
// ----------------------------------------------------------------------------
#define COLOR_BG         0x0842  // Dark midnight navy
#define COLOR_PANEL      0x10A3  // Slate container
#define COLOR_PANEL_TEXT 0x0862  // Darker text container
#define COLOR_BORDER     0x03EF  // Neon cyan border
#define COLOR_CYAN       0x07FF  // Bright glowing cyan
#define COLOR_MAGENTA    0xF81F  // Electric magenta
#define COLOR_YELLOW     0xFFE0  // Bright amber yellow
#define COLOR_GREEN      0x07E0  // Matrix green
#define COLOR_WHITE      0xFFFF  // Crisp white
#define COLOR_GRAY       0x7BEF  // Dim gray

TFT_eSPI tft = TFT_eSPI();

// ----------------------------------------------------------------------------
// llama2.c Transformer Architecture
// ----------------------------------------------------------------------------
typedef struct {
    int dim;
    int hidden_dim;
    int n_layers;
    int n_heads;
    int n_kv_heads;
    int vocab_size;
    int seq_len;
} Config;

typedef struct {
    float* token_embedding_table;
    float* rms_att_weight;
    float* rms_ffn_weight;
    float* wq;
    float* wk;
    float* wv;
    float* wo;
    float* w1;
    float* w2;
    float* w3;
    float* rms_final_weight;
    float* wcls;
} TransformerWeights;

typedef struct {
    float *x;
    float *xb;
    float *xb2;
    float *hb;
    float *hb2;
    float *q;
    float *att;
    float *logits;
    float* key_cache;
    float* value_cache;
} RunState;

typedef struct {
    Config config;
    TransformerWeights weights;
    RunState state;
} Transformer;

// ----------------------------------------------------------------------------
// Tokenizer
// ----------------------------------------------------------------------------
typedef struct {
    char** vocab;
    float* vocab_scores;
    int vocab_size;
    unsigned int max_token_length;
    unsigned char byte_pieces[512];
} Tokenizer;

// ----------------------------------------------------------------------------
// Sampler
// ----------------------------------------------------------------------------
typedef struct {
    int vocab_size;
    float temperature;
    float topp;
    unsigned long long rng_state;
} Sampler;

// ----------------------------------------------------------------------------
// Global Engine Instances
// ----------------------------------------------------------------------------
Transformer transformer;
Tokenizer tokenizer;
Sampler sampler;

// FreeRTOS Inter-Core Messaging
struct TokenMsg {
    char text[24];
    bool is_eos;
    float tok_per_sec;
};

QueueHandle_t token_queue;
TaskHandle_t llm_task_handle = NULL;

// Active Prompt Request Flag
volatile int pending_prompt_id = -1;
volatile bool is_generating = false;

// ----------------------------------------------------------------------------
// Companion State & Expressions
// ----------------------------------------------------------------------------
enum EyeState {
    EYE_IDLE,
    EYE_BLINK,
    EYE_THINKING,
    EYE_SPEAKING,
    EYE_HAPPY
};

EyeState current_eye_state = EYE_IDLE;
unsigned long last_blink_time = 0;
unsigned long blink_duration = 140;
bool is_blinking = false;
int look_offset_x = 0;
int mouth_wave_step = 0;

// Text Terminal Cursor Tracking
int cursor_x = 122;
int cursor_y = 52;
const int text_box_x = 114;
const int text_box_y = 32;
const int text_box_w = 198;
const int text_box_h = 146;
const int line_height = 13;

// ----------------------------------------------------------------------------
// Hardware Helper Functions
// ----------------------------------------------------------------------------
void set_cyd_led(bool r, bool g, bool b) {
    // CYD RGB LED is active-LOW
    digitalWrite(CYD_LED_RED, r ? LOW : HIGH);
    digitalWrite(CYD_LED_RED_ALT, r ? LOW : HIGH);
    digitalWrite(CYD_LED_GREEN, g ? LOW : HIGH);
    digitalWrite(CYD_LED_BLUE, b ? LOW : HIGH);
}

void init_cyd_touch() {
    pinMode(XPT_CLK, OUTPUT);
    pinMode(XPT_MOSI, OUTPUT);
    pinMode(XPT_MISO, INPUT);
    pinMode(XPT_CS, OUTPUT);
    pinMode(XPT_IRQ, INPUT); // Pin 36 is an input-only pad without internal pullup
    digitalWrite(XPT_CS, HIGH);
    digitalWrite(XPT_CLK, LOW);
}

static uint16_t xpt_transfer(uint8_t cmd) {
    uint16_t result = 0;
    for (int i = 7; i >= 0; i--) {
        digitalWrite(XPT_MOSI, (cmd >> i) & 1);
        digitalWrite(XPT_CLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(XPT_CLK, LOW);
        delayMicroseconds(1);
    }
    for (int i = 11; i >= 0; i--) {
        digitalWrite(XPT_CLK, HIGH);
        delayMicroseconds(1);
        if (digitalRead(XPT_MISO)) result |= (1 << i);
        digitalWrite(XPT_CLK, LOW);
        delayMicroseconds(1);
    }
    return result;
}

bool get_cyd_touch(int *screen_x, int *screen_y) {
    if (digitalRead(XPT_IRQ) != LOW) return false;
    digitalWrite(XPT_CS, LOW);
    delayMicroseconds(2);
    // Send 0xD0 (Read X) & 0x90 (Read Y)
    uint16_t raw_x = xpt_transfer(0xD0);
    uint16_t raw_y = xpt_transfer(0x90);
    digitalWrite(XPT_CS, HIGH);

    if (raw_x < 200 || raw_x > 3900 || raw_y < 200 || raw_y > 3900) return false;

    // CYD Landscape (Rotation 1):
    // raw_y is the horizontal 320px axis
    // raw_x is the vertical 240px axis
    *screen_x = map(raw_y, 3650, 320, 0, 320);
    *screen_y = map(raw_x, 3650, 320, 0, 240);

    *screen_x = constrain(*screen_x, 0, 319);
    *screen_y = constrain(*screen_y, 0, 239);
    return true;
}

// ----------------------------------------------------------------------------
// llama2.c Math & Transformer Core
// ----------------------------------------------------------------------------
void rmsnorm(float* o, float* x, float* weight, int size) {
    float ss = 0.0f;
    for (int j = 0; j < size; j++) {
        ss += x[j] * x[j];
    }
    ss /= size;
    ss += 1e-5f;
    ss = 1.0f / sqrtf(ss);
    for (int j = 0; j < size; j++) {
        o[j] = weight[j] * (ss * x[j]);
    }
}

void softmax(float* x, int size) {
    float max_val = x[0];
    for (int i = 1; i < size; i++) {
        if (x[i] > max_val) max_val = x[i];
    }
    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
        x[i] = expf(x[i] - max_val);
        sum += x[i];
    }
    for (int i = 0; i < size; i++) {
        x[i] /= sum;
    }
}

void matmul(float* xout, float* x, float* w, int n, int d) {
    for (int i = 0; i < d; i++) {
        float val = 0.0f;
        for (int j = 0; j < n; j++) {
            val += w[i * n + j] * x[j];
        }
        xout[i] = val;
    }
}

float* forward(Transformer* t, int token, int pos) {
    Config* p = &t->config;
    TransformerWeights* w = &t->weights;
    RunState* s = &t->state;
    float *x = s->x;
    int dim = p->dim;
    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;
    int kv_mul = p->n_heads / p->n_kv_heads;
    int hidden_dim = p->hidden_dim;
    int head_size = dim / p->n_heads;

    // 1. Embedding lookup
    float* content_row = w->token_embedding_table + token * dim;
    memcpy(x, content_row, dim * sizeof(float));

    // 2. Transformer layers
    for (int l = 0; l < p->n_layers; l++) {
        rmsnorm(s->xb, x, w->rms_att_weight + l * dim, dim);

        // Q, K, V Projections
        matmul(s->q, s->xb, w->wq + l * dim * dim, dim, dim);
        matmul(s->key_cache + l * p->seq_len * kv_dim + pos * kv_dim, s->xb, w->wk + l * dim * kv_dim, dim, kv_dim);
        matmul(s->value_cache + l * p->seq_len * kv_dim + pos * kv_dim, s->xb, w->wv + l * dim * kv_dim, dim, kv_dim);

        // RoPE relative positional encoding
        for (int i = 0; i < dim; i += 2) {
            int head_dim = i % head_size;
            float freq = 1.0f / powf(10000.0f, head_dim / (float)head_size);
            float val = pos * freq;
            float fcr = cosf(val);
            float fci = sinf(val);
            int rotn = i < kv_dim ? 2 : 1;
            for (int v = 0; v < rotn; v++) {
                float* vec = (v == 0) ? s->q : (s->key_cache + l * p->seq_len * kv_dim + pos * kv_dim);
                float v0 = vec[i];
                float v1 = vec[i + 1];
                vec[i]   = v0 * fcr - v1 * fci;
                vec[i + 1] = v0 * fci + v1 * fcr;
            }
        }

        // Multi-head attention
        for (int h = 0; h < p->n_heads; h++) {
            float* q = s->q + h * head_size;
            float* att = s->att + h * p->seq_len;
            for (int t = 0; t <= pos; t++) {
                float* k = s->key_cache + l * p->seq_len * kv_dim + t * kv_dim + (h / kv_mul) * head_size;
                float score = 0.0f;
                for (int i = 0; i < head_size; i++) {
                    score += q[i] * k[i];
                }
                score /= sqrtf(head_size);
                att[t] = score;
            }

            softmax(att, pos + 1);

            float* xb = s->xb + h * head_size;
            memset(xb, 0, head_size * sizeof(float));
            for (int t = 0; t <= pos; t++) {
                float* v = s->value_cache + l * p->seq_len * kv_dim + t * kv_dim + (h / kv_mul) * head_size;
                float a = att[t];
                for (int i = 0; i < head_size; i++) {
                    xb[i] += a * v[i];
                }
            }
        }

        // Output projection
        matmul(s->xb2, s->xb, w->wo + l * dim * dim, dim, dim);
        for (int i = 0; i < dim; i++) x[i] += s->xb2[i];

        // FFN RMSNorm
        rmsnorm(s->xb, x, w->rms_ffn_weight + l * dim, dim);
        matmul(s->hb, s->xb, w->w1 + l * dim * hidden_dim, dim, hidden_dim);
        matmul(s->hb2, s->xb, w->w3 + l * dim * hidden_dim, dim, hidden_dim);

        // SwiGLU activation
        for (int i = 0; i < hidden_dim; i++) {
            float val = s->hb[i];
            val *= (1.0f / (1.0f + expf(-val))); // SiLU
            val *= s->hb2[i];
            s->hb[i] = val;
        }

        matmul(s->xb, s->hb, w->w2 + l * dim * hidden_dim, hidden_dim, dim);
        for (int i = 0; i < dim; i++) x[i] += s->xb[i];
    }

    // Final RMSNorm & classifier
    rmsnorm(x, x, w->rms_final_weight, dim);
    matmul(s->logits, x, w->wcls, p->dim, p->vocab_size);
    return s->logits;
}

// ----------------------------------------------------------------------------
// Model Loading from Flash Memory
// ----------------------------------------------------------------------------
void memory_map_weights(TransformerWeights *w, Config* p, float* ptr, int shared_weights) {
    int head_size = p->dim / p->n_heads;
    int n_layers = p->n_layers;

    w->token_embedding_table = ptr;
    ptr += p->vocab_size * p->dim;

    w->rms_att_weight = ptr;
    ptr += n_layers * p->dim;

    w->wq = ptr;
    ptr += n_layers * p->dim * p->dim;

    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;
    w->wk = ptr;
    ptr += n_layers * p->dim * kv_dim;

    w->wv = ptr;
    ptr += n_layers * p->dim * kv_dim;

    w->wo = ptr;
    ptr += n_layers * p->dim * p->dim;

    w->rms_ffn_weight = ptr;
    ptr += n_layers * p->dim;

    w->w1 = ptr;
    ptr += n_layers * p->dim * p->hidden_dim;

    w->w2 = ptr;
    ptr += n_layers * p->hidden_dim * p->dim;

    w->w3 = ptr;
    ptr += n_layers * p->dim * p->hidden_dim;

    w->rms_final_weight = ptr;
    ptr += p->dim;

    // Skip unused precomputed RoPE tables
    ptr += p->seq_len * head_size / 2;
    ptr += p->seq_len * head_size / 2;

    w->wcls = shared_weights ? w->token_embedding_table : ptr;
}

bool malloc_run_state(RunState* s, Config* p) {
    int kv_dim = (p->dim * p->n_kv_heads) / p->n_heads;

    s->x = (float*)calloc(p->dim, sizeof(float));
    s->xb = (float*)calloc(p->dim, sizeof(float));
    s->xb2 = (float*)calloc(p->dim, sizeof(float));
    s->hb = (float*)calloc(p->hidden_dim, sizeof(float));
    s->hb2 = (float*)calloc(p->hidden_dim, sizeof(float));
    s->q = (float*)calloc(p->dim, sizeof(float));

    // Cap KV cache to seq_len (192 tokens) - uses ~245KB RAM, fits safely in SRAM
    s->key_cache = (float*)calloc(p->n_layers * p->seq_len * kv_dim, sizeof(float));
    s->value_cache = (float*)calloc(p->n_layers * p->seq_len * kv_dim, sizeof(float));
    s->att = (float*)calloc(p->n_heads * p->seq_len, sizeof(float));
    s->logits = (float*)calloc(p->vocab_size, sizeof(float));

    return (s->x && s->xb && s->xb2 && s->hb && s->hb2 && s->q &&
            s->key_cache && s->value_cache && s->att && s->logits);
}

void init_model() {
    Serial.println("[LLM] Initializing transformer from Flash memory...");
    Config* orig_config = (Config*)stories260k_data;

    transformer.config = *orig_config;
    int shared_weights = (transformer.config.vocab_size > 0) ? 1 : 0;
    transformer.config.vocab_size = abs(transformer.config.vocab_size);

    // Sequence length = 128 tokens fits safely within the ESP32 internal SRAM
    transformer.config.seq_len = 128;

    Serial.printf("[LLM] Config: dim=%d, hidden_dim=%d, layers=%d, heads=%d, vocab=%d, seq_len=%d\n",
                  transformer.config.dim, transformer.config.hidden_dim,
                  transformer.config.n_layers, transformer.config.n_heads,
                  transformer.config.vocab_size, transformer.config.seq_len);

    float* weights_ptr = (float*)stories260k_data + sizeof(Config)/sizeof(float);
    memory_map_weights(&transformer.weights, &transformer.config, weights_ptr, shared_weights);

    if (!malloc_run_state(&transformer.state, &transformer.config)) {
        Serial.println("[LLM] ERROR: Failed to allocate RunState in RAM!");
    } else {
        Serial.printf("[LLM] Transformer allocated successfully. Free Heap: %d bytes\n", ESP.getFreeHeap());
    }
}

// ----------------------------------------------------------------------------
// Tokenizer Loader
// ----------------------------------------------------------------------------
void init_tokenizer() {
    Serial.println("[TOK] Initializing tokenizer from Flash memory...");
    int vocab_size = transformer.config.vocab_size;
    tokenizer.vocab_size = vocab_size;
    tokenizer.vocab = (char**)malloc(vocab_size * sizeof(char*));
    tokenizer.vocab_scores = (float*)malloc(vocab_size * sizeof(float));

    for (int i = 0; i < 256; i++) {
        tokenizer.byte_pieces[i * 2] = (unsigned char)i;
        tokenizer.byte_pieces[i * 2 + 1] = '\0';
    }

    int offset = 0;
    memcpy(&tokenizer.max_token_length, tok512_data + offset, sizeof(int));
    offset += sizeof(int);

    for (int i = 0; i < vocab_size; i++) {
        memcpy(tokenizer.vocab_scores + i, tok512_data + offset, sizeof(float));
        offset += sizeof(float);
        int len;
        memcpy(&len, tok512_data + offset, sizeof(int));
        offset += sizeof(int);

        tokenizer.vocab[i] = (char*)malloc(len + 1);
        memcpy(tokenizer.vocab[i], tok512_data + offset, len);
        offset += len;
        tokenizer.vocab[i][len] = '\0';
    }
    Serial.println("[TOK] Tokenizer loaded.");
}

char* decode_token(Tokenizer* t, int prev_token, int token) {
    char *piece = t->vocab[token];
    if (prev_token == 1 && piece[0] == ' ') { piece++; }
    unsigned char byte_val;
    if (sscanf(piece, "<0x%02hhX>", &byte_val) == 1) {
        piece = (char*)t->byte_pieces + byte_val * 2;
    }
    return piece;
}

// ----------------------------------------------------------------------------
// Sampler Implementation
// ----------------------------------------------------------------------------
unsigned int random_u32(unsigned long long *state) {
    *state ^= *state >> 12;
    *state ^= *state << 25;
    *state ^= *state >> 27;
    return (*state * 0x2545F4914F6CDD1DULL) >> 32;
}

float random_f32(unsigned long long *state) {
    return (random_u32(state) >> 8) / 16777216.0f;
}

int sample_token(Sampler* s, float* logits) {
    if (s->temperature == 0.0f) {
        int max_i = 0;
        float max_p = logits[0];
        for (int i = 1; i < s->vocab_size; i++) {
            if (logits[i] > max_p) { max_i = i; max_p = logits[i]; }
        }
        return max_i;
    }

    for (int q = 0; q < s->vocab_size; q++) logits[q] /= s->temperature;
    softmax(logits, s->vocab_size);

    float coin = random_f32(&s->rng_state);
    float cdf = 0.0f;
    for (int i = 0; i < s->vocab_size; i++) {
        cdf += logits[i];
        if (coin < cdf) return i;
    }
    return s->vocab_size - 1;
}

// ----------------------------------------------------------------------------
// Verified Exact BPE Prompts for TinyStories Vocabulary (tok512)
// ----------------------------------------------------------------------------
// 1. "Once upon a time"
const int PROMPT_STORY[]  = {1, 441, 416, 331, 407, 261, 378};
// 2. "One day, Lily"
const int PROMPT_SPARK[]  = {1, 441, 416, 411, 328, 432, 317};
// 3. "Tim and his dog"
const int PROMPT_PET[]    = {1, 434, 288, 269, 345, 400, 428};
// 4. "There was a"
const int PROMPT_RANDOM[] = {1, 434, 386, 411, 286, 261};

// ----------------------------------------------------------------------------
// Core 0: LLM Inference Task
// ----------------------------------------------------------------------------
void llm_task(void *pvParameters) {
    sampler.vocab_size = transformer.config.vocab_size;
    sampler.temperature = 0.70f; // 0.70 produces coherent, structured stories
    sampler.topp = 0.9f;
    sampler.rng_state = 133742ULL + esp_random();

    for (;;) {
        if (pending_prompt_id >= 0) {
            int prompt_id = pending_prompt_id;
            pending_prompt_id = -1;
            is_generating = true;

            const int* prompt_tokens = PROMPT_STORY;
            int num_prompt_tokens = 7;

            if (prompt_id == 1) {
                prompt_tokens = PROMPT_SPARK;
                num_prompt_tokens = 7;
            } else if (prompt_id == 2) {
                prompt_tokens = PROMPT_PET;
                num_prompt_tokens = 7;
            } else if (prompt_id == 3) {
                prompt_tokens = PROMPT_RANDOM;
                num_prompt_tokens = 6;
                sampler.rng_state += esp_random();
            }

            int token = prompt_tokens[0];
            int prev_token = 0;
            int pos = 0;

            unsigned long start_time = millis();
            int gen_count = 0;

            while (pos < transformer.config.seq_len) {
                float* logits = forward(&transformer, token, pos);

                int next_token;
                if (pos < num_prompt_tokens - 1) {
                    next_token = prompt_tokens[pos + 1];
                } else {
                    next_token = sample_token(&sampler, logits);
                }

                // Check EOS token (token 2 is </s>)
                if (next_token == 2 && pos >= num_prompt_tokens) {
                    break;
                }

                char* piece = decode_token(&tokenizer, token, next_token);

                gen_count++;
                float elapsed = (millis() - start_time) / 1000.0f;
                float tok_speed = (elapsed > 0) ? (gen_count / elapsed) : 0;

                TokenMsg msg;
                strncpy(msg.text, piece, sizeof(msg.text) - 1);
                msg.text[sizeof(msg.text) - 1] = '\0';
                msg.is_eos = false;
                msg.tok_per_sec = tok_speed;

                xQueueSend(token_queue, &msg, portMAX_DELAY);
                Serial.print(piece);

                prev_token = token;
                token = next_token;
                pos++;

                vTaskDelay(pdMS_TO_TICKS(4));
            }

            // Print graceful conclusion
            TokenMsg end_msg;
            strncpy(end_msg.text, "\n\n[THE END] *", sizeof(end_msg.text) - 1);
            end_msg.text[sizeof(end_msg.text) - 1] = '\0';
            end_msg.is_eos = false;
            end_msg.tok_per_sec = 0;
            xQueueSend(token_queue, &end_msg, portMAX_DELAY);

            // Send EOS signal
            TokenMsg eos_msg;
            eos_msg.text[0] = '\0';
            eos_msg.is_eos = true;
            eos_msg.tok_per_sec = 0;
            xQueueSend(token_queue, &eos_msg, portMAX_DELAY);

            Serial.println("\n[LLM] Generation finished.");
            is_generating = false;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ----------------------------------------------------------------------------
// Companion Avatar Face Drawing Routines (Core 1)
// ----------------------------------------------------------------------------
void draw_avatar_frame() {
    tft.fillRoundRect(8, 32, 100, 146, 8, COLOR_PANEL);
    tft.drawRoundRect(8, 32, 100, 146, 8, COLOR_BORDER);
    tft.drawFastHLine(14, 40, 20, COLOR_CYAN);
    tft.drawFastHLine(74, 40, 20, COLOR_CYAN);
}

void draw_companion_eyes(EyeState state) {
    int eye_w = 26;
    int eye_h = 36;
    int eye1_x = 22;
    int eye2_x = 60;
    int eye_y = 66;

    tft.fillRect(16, 56, 84, 56, COLOR_PANEL);

    switch (state) {
        case EYE_BLINK:
            tft.fillRoundRect(eye1_x, eye_y + 16, eye_w, 4, 2, COLOR_CYAN);
            tft.fillRoundRect(eye2_x, eye_y + 16, eye_w, 4, 2, COLOR_CYAN);
            break;

        case EYE_THINKING:
            tft.fillRoundRect(eye1_x, eye_y + 8, eye_w, 18, 4, COLOR_YELLOW);
            tft.fillRoundRect(eye2_x, eye_y + 8, eye_w, 18, 4, COLOR_YELLOW);
            tft.fillRoundRect(eye1_x + 4, eye_y + 12, 18, 4, 2, COLOR_WHITE);
            tft.fillRoundRect(eye2_x + 4, eye_y + 12, 18, 4, 2, COLOR_WHITE);
            break;

        case EYE_HAPPY:
            tft.drawCircle(eye1_x + 13, eye_y + 14, 12, COLOR_MAGENTA);
            tft.fillRect(eye1_x, eye_y + 14, eye_w, 14, COLOR_PANEL);
            tft.drawCircle(eye2_x + 13, eye_y + 14, 12, COLOR_MAGENTA);
            tft.fillRect(eye2_x, eye_y + 14, eye_w, 14, COLOR_PANEL);
            break;

        case EYE_SPEAKING:
        case EYE_IDLE:
        default:
            tft.fillRoundRect(eye1_x, eye_y, eye_w, eye_h, 8, COLOR_CYAN);
            tft.fillRoundRect(eye2_x, eye_y, eye_w, eye_h, 8, COLOR_CYAN);
            tft.fillCircle(eye1_x + 8 + look_offset_x, eye_y + 10, 4, COLOR_WHITE);
            tft.fillCircle(eye2_x + 8 + look_offset_x, eye_y + 10, 4, COLOR_WHITE);
            break;
    }
}

void draw_mouth_bars(bool speaking) {
    int mouth_x = 30;
    int mouth_y = 145;

    tft.fillRect(mouth_x - 4, mouth_y - 12, 56, 24, COLOR_PANEL);

    if (speaking) {
        int heights[] = {4, 12, 18, 14, 6};
        int offset = mouth_wave_step % 5;
        for (int i = 0; i < 5; i++) {
            int h = heights[(i + offset) % 5];
            tft.fillRoundRect(mouth_x + i * 10, mouth_y - (h / 2), 6, h, 2, COLOR_MAGENTA);
        }
    } else {
        tft.fillRoundRect(mouth_x + 8, mouth_y, 32, 4, 2, COLOR_CYAN);
    }
}

// ----------------------------------------------------------------------------
// Dialogue Speech Box & Page-by-Page Terminal
// ----------------------------------------------------------------------------
void draw_speech_panel() {
    tft.fillRoundRect(text_box_x, text_box_y, text_box_w, text_box_h, 8, COLOR_PANEL_TEXT);
    tft.drawRoundRect(text_box_x, text_box_y, text_box_w, text_box_h, 8, COLOR_BORDER);

    tft.setTextSize(1);
    tft.setTextColor(COLOR_CYAN, COLOR_PANEL_TEXT);
    tft.drawString("COMPANION LOG // REAL-TIME", text_box_x + 8, text_box_y + 6);
    tft.drawFastHLine(text_box_x + 4, text_box_y + 18, text_box_w - 8, COLOR_BORDER);

    cursor_x = text_box_x + 8;
    cursor_y = text_box_y + 24;
}

void clear_speech_text() {
    tft.fillRect(text_box_x + 2, text_box_y + 20, text_box_w - 4, text_box_h - 22, COLOR_PANEL_TEXT);
    cursor_x = text_box_x + 8;
    cursor_y = text_box_y + 24;
}

void print_speech_token(const char* str) {
    tft.setTextSize(1);
    tft.setTextColor(COLOR_WHITE, COLOR_PANEL_TEXT);

    int len = strlen(str);
    for (int i = 0; i < len; i++) {
        char c = str[i];
        if (c == '\n') {
            cursor_x = text_box_x + 8;
            cursor_y += line_height;
        } else {
            if (cursor_x + 6 > text_box_x + text_box_w - 8) {
                cursor_x = text_box_x + 8;
                cursor_y += line_height;
            }
            // When box is filled, pause 1.2s to let user read, then clear and continue
            if (cursor_y + line_height > text_box_y + text_box_h - 6) {
                delay(1200);
                clear_speech_text();
            }
            tft.drawChar(c, cursor_x, cursor_y);
            cursor_x += 6;
        }
    }
}

// ----------------------------------------------------------------------------
// Top Header & Bottom Buttons
// ----------------------------------------------------------------------------
void draw_header(float tok_speed) {
    tft.fillRect(0, 0, 320, 28, COLOR_BG);

    tft.fillCircle(12, 14, 4, is_generating ? COLOR_YELLOW : COLOR_GREEN);
    tft.setTextSize(1);
    tft.setTextColor(is_generating ? COLOR_YELLOW : COLOR_GREEN, COLOR_BG);
    tft.drawString(is_generating ? "TELLING STORY" : "ONLINE", 22, 10);

    tft.setTextColor(COLOR_CYAN, COLOR_BG);
    tft.drawString("LLAMA-2 [260K]", 120, 10);

    tft.setTextColor(COLOR_GRAY, COLOR_BG);
    if (tok_speed > 0) {
        char buf[20];
        snprintf(buf, sizeof(buf), "%.1f T/S", tok_speed);
        tft.drawString(buf, 260, 10);
    } else {
        tft.drawString("TAP TO TALK", 240, 10);
    }
}

struct TouchBtn {
    int x;
    int y;
    int w;
    int h;
    const char* label;
    uint16_t color;
};

TouchBtn buttons[4] = {
    { 6,   186, 74, 46, "STORY",  COLOR_CYAN   },
    { 84,  186, 74, 46, "SPARK",  COLOR_MAGENTA},
    { 162, 186, 74, 46, "PET",    COLOR_YELLOW },
    { 240, 186, 74, 46, "RANDOM", COLOR_GREEN  }
};

void draw_touch_buttons(int active_btn = -1) {
    tft.setTextSize(1);
    for (int i = 0; i < 4; i++) {
        uint16_t bg = (active_btn == i) ? buttons[i].color : COLOR_PANEL;
        uint16_t fg = (active_btn == i) ? COLOR_BG : buttons[i].color;

        tft.fillRoundRect(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, 6, bg);
        tft.drawRoundRect(buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h, 6, buttons[i].color);

        int text_x = buttons[i].x + (buttons[i].w / 2) - (strlen(buttons[i].label) * 3);
        int text_y = buttons[i].y + 18;
        tft.setTextColor(fg, bg);
        tft.drawString(buttons[i].label, text_x, text_y);
    }
}

// ----------------------------------------------------------------------------
// Setup & Main Loop (Core 1)
// ----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println("\n=============================================");
    Serial.println("   ESP32 CYD AI COMPANION (llama2.c v1.1)    ");
    Serial.println("=============================================");

    pinMode(CYD_LED_RED, OUTPUT);
    pinMode(CYD_LED_RED_ALT, OUTPUT);
    pinMode(CYD_LED_GREEN, OUTPUT);
    pinMode(CYD_LED_BLUE, OUTPUT);
    set_cyd_led(false, false, false);

    pinMode(CYD_TFT_BL, OUTPUT);
    digitalWrite(CYD_TFT_BL, HIGH);

    init_cyd_touch();

    tft.init();
    tft.setRotation(1); // 320x240 Landscape
    tft.fillScreen(COLOR_BG);

    tft.setTextColor(COLOR_CYAN, COLOR_BG);
    tft.setTextSize(2);
    tft.drawString("CYD AI COMPANION", 45, 90);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_WHITE, COLOR_BG);
    tft.drawString("Loading Llama-2 weights from Flash...", 45, 125);

    init_model();
    init_tokenizer();

    token_queue = xQueueCreate(32, sizeof(TokenMsg));

    xTaskCreatePinnedToCore(
        llm_task,
        "llm_task",
        8192,
        NULL,
        1,
        &llm_task_handle,
        0
    );

    tft.fillScreen(COLOR_BG);
    draw_header(0);
    draw_avatar_frame();
    draw_companion_eyes(EYE_IDLE);
    draw_mouth_bars(false);
    draw_speech_panel();
    draw_touch_buttons();

    print_speech_token("Hello! I am your CYD AI Companion.\nTap any of the 4 buttons below:");
    print_speech_token("\n* STORY  : Fairy tale");
    print_speech_token("\n* SPARK  : Lily's day");
    print_speech_token("\n* PET    : Tim's dog");
    print_speech_token("\n* RANDOM : Wild surprise");

    set_cyd_led(false, true, false);
    delay(300);
    set_cyd_led(false, false, false);
}

void loop() {
    unsigned long now = millis();

    // 1. Process tokens from Core 0
    TokenMsg msg;
    if (xQueueReceive(token_queue, &msg, 0) == pdTRUE) {
        if (!msg.is_eos) {
            current_eye_state = EYE_SPEAKING;
            mouth_wave_step++;
            draw_companion_eyes(EYE_SPEAKING);
            draw_mouth_bars(true);
            print_speech_token(msg.text);
            draw_header(msg.tok_per_sec);

            set_cyd_led(false, (mouth_wave_step % 2 == 0), true);
        } else {
            current_eye_state = EYE_IDLE;
            draw_companion_eyes(EYE_IDLE);
            draw_mouth_bars(false);
            draw_header(0);
            set_cyd_led(false, false, false);
        }
    }

    // 2. Idle animations
    if (!is_generating) {
        if (!is_blinking && (now - last_blink_time > 3200)) {
            is_blinking = true;
            last_blink_time = now;
            draw_companion_eyes(EYE_BLINK);
        } else if (is_blinking && (now - last_blink_time > blink_duration)) {
            is_blinking = false;
            last_blink_time = now;

            int r = random(0, 3);
            if (r == 0) look_offset_x = -2;
            else if (r == 1) look_offset_x = 2;
            else look_offset_x = 0;

            draw_companion_eyes(EYE_IDLE);
        }
    }

    // 3. Touch Input Handling
    int touch_x, touch_y;
    if (get_cyd_touch(&touch_x, &touch_y)) {
        if (!is_generating) {
            // Check if touch is in the bottom button area (Y >= 165)
            if (touch_y >= 165) {
                int btn_idx = -1;
                if (touch_x < 80) {
                    btn_idx = 0; // STORY
                } else if (touch_x < 160) {
                    btn_idx = 1; // SPARK
                } else if (touch_x < 240) {
                    btn_idx = 2; // PET
                } else {
                    btn_idx = 3; // RANDOM
                }

                if (btn_idx >= 0) {
                    Serial.printf("[TOUCH] Screen (%d, %d) -> Trigger Button %d (%s)\n",
                                  touch_x, touch_y, btn_idx, buttons[btn_idx].label);

                    draw_touch_buttons(btn_idx);
                    set_cyd_led(true, false, true);

                    draw_companion_eyes(EYE_HAPPY);
                    delay(150);

                    clear_speech_text();
                    current_eye_state = EYE_THINKING;
                    draw_companion_eyes(EYE_THINKING);

                    draw_touch_buttons(-1);

                    pending_prompt_id = btn_idx;
                }
            }
        }
    }

    delay(20);
}
