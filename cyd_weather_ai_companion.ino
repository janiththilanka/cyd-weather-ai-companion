/*
 * ============================================================================
 * CYD AI WEATHER ANALYZER, CLOCK & COMPANION AGENT — v2.1
 * Architecture: Calm, high-legibility, flicker-free UI Specification v2.1
 * Hardware: ESP32-2432S028R ("Cheap Yellow Display" / CYD)
 *
 * Primary Display:   2.8" TFT 320x240, ILI9341/ST7789 (SPI @ 40 MHz)
 * Secondary Display: 0.96" OLED 128x64, SSD1306 (I2C SDA:27, SCL:22, addr 0x3C)
 * Touchscreen:       XPT2046 Resistive Touch (SPI, CS:33, IRQ:36)
 * Backlight:         LEDC PWM ch0, 5 kHz, GPIO 21
 * RGB LED:           Active LOW (R:4, G:16, B:17)
 * MicroSD:           CS:5 (Driven HIGH to prevent SPI bus contention)
 *
 * Screen Navigation Model:
 *   [Page 0] Main Display Clock: Large ambient hero clock, date, weather glance
 *   [Page 1] One Touch -> Clock with Today Weather & AI Companion:
 *            Compact clock, live weather, animated AI face, AI Advisory card
 *   [Page 2] Two Touch -> Forecast Page: Full 4-day synoptic forecast analyzer
 *   [Loop]   Third Touch -> Returns to Page 0 Main Clock
 *   [Night]  Mode C: Auto/manual warm amber night clock with dimmed backlight
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <math.h>
#include <Preferences.h>

// ----------------------------------------------------------------------------
// Hardware Pinout (ESP32-2432S028R)
// ----------------------------------------------------------------------------
#define CYD_TFT_BL        21    // Backlight PWM pin
#define CYD_LDR_PIN       34    // Onboard Light Sensor (Analog ADC1_CH6)
#define CYD_SD_CS         5     // MicroSD Card CS (Must be driven HIGH)

// RGB LED (Common anode, active LOW)
#define CYD_LED_RED       4
#define CYD_LED_GREEN     16
#define CYD_LED_BLUE      17

// CYD Touch Pins (XPT2046 Software SPI)
#define XPT_CLK           25
#define XPT_CS            33
#define XPT_MOSI          32
#define XPT_MISO          39
#define XPT_IRQ           36

// Secondary 128x64 OLED I2C Pins (SSD1306)
#define OLED_SDA          27
#define OLED_SCL          22
#define OLED_WIDTH        128
#define OLED_HEIGHT       64
#define OLED_RESET        -1
#define OLED_ADDR1        0x3C
#define OLED_ADDR2        0x3D

// ----------------------------------------------------------------------------
// Color Palette Tokens (RGB565 Verified - Spec v2.0 §4.1)
// ----------------------------------------------------------------------------
#define C_BG              0x0862   // #0B0F14 (Deep cosmic slate)
#define C_SURFACE         0x10C4   // #141A22 (Card background fill)
#define C_SURFACE_ALT     0x1926   // #1C2430 (Chips, speech panel)
#define C_BORDER          0x2188   // #263241 (Card borders, dividers)

#define C_TEXT_HI         0xF7BF   // #F2F6FA (Primary high-contrast text)
#define C_TEXT_MID        0x9D36   // #9AA7B4 (Labels, subtitles)
#define C_TEXT_LOW        0x5B4F   // #5C6B7A (Disabled, hint text)

#define C_ACCENT          0x3DFF   // #38BDF8 (Neon Sky Accent)
#define C_ACCENT_DIM      0x1B72   // #1E6F94 (Dimmed accent)
#define C_OK              0x3693   // #34D399 (Emerald Green)
#define C_WARN            0xFDE4   // #FBBF24 (Amber/Yellow)
#define C_DANGER          0xFB8E   // #F87171 (Coral Red)
#define C_TEAL            0x2EB7   // #2DD4BF (Mint/Teal)

#define C_NIGHT_AMBER     0xFDA9   // #FFB74D (Soft Night Amber)
#define C_NIGHT_DIM       0x3A2A   // #3A4750 (Night secondary text)
#define C_NIGHT_FAINT     0x2146   // #232A31 (Night hint text)

#define C_BLACK           0x0000
#define C_WHITE           0xFFFF

// ----------------------------------------------------------------------------
// Network, NTP & Location Configuration
// ----------------------------------------------------------------------------
const char* WIFI_SSID       = "COD-Guest";
const char* WIFI_PASS       = "";   // Leave empty for open network, or add password

// NTP Configuration (UTC +05:30 for Sri Jayawardhanapura Kotte)
const char* NTP_SERVER1     = "pool.ntp.org";
const char* NTP_SERVER2     = "time.google.com";
const long  GMT_OFFSET_SEC  = 19800; // 5.5 hours * 3600 = 19800s
const int   DAYLIGHT_OFFSET = 0;

// Weather Telemetry (Open-Meteo for Sri Jayawardhanapura Kotte, Sri Lanka)
const char* LOCATION_NAME   = "Kotte, LK";
const float LOC_LAT         = 6.9016f;
const float LOC_LON         = 79.9077f;
const char* LOC_TIMEZONE    = "Asia%2FColombo";

// Weather sync interval: 15 minutes
const unsigned long WEATHER_SYNC_INTERVAL_MS = 15 * 60 * 1000UL;

// Night Mode Schedule (22:00 Bedtime to 06:30 Sunrise)
const int NIGHT_START_H     = 22;
const int NIGHT_START_M     = 0;
const int NIGHT_END_H       = 6;
const int NIGHT_END_M       = 30;

// ----------------------------------------------------------------------------
// UI State Machines & Data Structures
// ----------------------------------------------------------------------------
enum DisplayMode {
    MODE_CLOCK = 0,      // Page 0: Main Display Clock (Hero clock, date, weather glance)
    MODE_COMPANION = 1,  // Page 1: Clock with Today Weather & AI Companion (Avatar, Insights)
    MODE_FORECAST = 2,   // Page 2: 4-Day Forecast Analyzer & Atmospheric Telemetry
    MODE_NIGHT = 3       // Mode C: Night Clock (Warm amber, dimmed backlight)
};

DisplayMode current_mode = MODE_CLOCK;

enum EyeEmotion {
    EMO_NEUTRAL,
    EMO_BLINK,
    EMO_HAPPY,
    EMO_THINKING,
    EMO_SLEEP,
    EMO_LISTENING,
    EMO_ALERT
};

EyeEmotion current_emotion = EMO_NEUTRAL;

// Weather Data Structure
struct WeatherData {
    bool valid = false;
    float temp_c = 28.5f;
    float feels_like_c = 31.0f;
    int humidity = 68;
    float precipitation_mm = 0.0f;
    int weather_code = 2; // Partly cloudy
    float wind_speed_kmh = 12.0f;
    float gust_kmh = 18.0f;
    float pressure_hpa = 1012.0f;
    float uv_index = 6.2f;
    bool is_day = true;

    // 4-day forecast
    float daily_max[4] = {31.0f, 29.0f, 27.0f, 30.0f};
    float daily_min[4] = {22.0f, 21.0f, 19.0f, 20.0f};
    int daily_code[4]  = {2, 3, 61, 80};
    String condition_text = "Partly cloudy";
};

WeatherData weather;
unsigned long last_weather_sync = 0;
bool weather_sync_in_progress = false;
bool weather_data_updated = false;

// Hardware Instances
TFT_eSPI tft = TFT_eSPI();
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
bool oled_available = false;

// Scheduling & Overrides
bool manual_mode_override = false;

// Avatar Gaze & Speech Animation Variables
float gaze_dx = 0.0f;
float gaze_dy = 0.0f;
unsigned long last_blink_time = 0;
bool is_blinking = false;
bool ai_is_speaking = false;
float mouth_level = 0.0f;

// AI Story Teller State Engine
int story_step = 0;                     // 0: Overview, 1: Atmosphere, 2: Advisory, 3: Forecast, 4: Summary, 5: End
const int TOTAL_STORY_STEPS = 6;        // 6 steps total (0..5)
unsigned long last_story_step_time = 0; // Timestamp of current step start
bool story_is_running = false;          // True while narrating (steps 0..4)
const unsigned long STORY_STEP_DURATION_MS = 3500; // 3.5 seconds per chapter

// AI Advisory & Details
char brief_line1[40] = "MORNING ADVISORY:";
char brief_line2[40] = "Carry a light umbrella";
char brief_line3[40] = "UV Index 6.2 · RH 68%";

// OLED Telemetry Buffer
uint8_t cpu_hist[26];
uint8_t hist_head = 0;

// Forward Declarations
void switch_mode(DisplayMode new_mode, struct tm &timeinfo);
void render_clock_mode(struct tm &timeinfo);
void render_companion_mode(struct tm &timeinfo);
void render_forecast_mode(struct tm &timeinfo);
void render_night_mode(struct tm &timeinfo);
void draw_parametric_icon(int cx, int cy, int code, bool is_day, int r = 12);
void draw_eyes_v2(EyeEmotion emotion, float dx, float dy);
void draw_mouth_equalizer_v2(float level);
void draw_text_story_viewer();
void draw_atmosphere_advisory_card();
void start_ai_story();
const char* get_short_weather_code_name(int code);
inline bool is_scheduled_night(const struct tm &timeinfo);

// ----------------------------------------------------------------------------
// CYD Hardware Control Helpers
// ----------------------------------------------------------------------------
void set_cyd_led(bool r, bool g, bool b) {
    digitalWrite(CYD_LED_RED,   r ? LOW : HIGH);
    digitalWrite(CYD_LED_GREEN, g ? LOW : HIGH);
    digitalWrite(CYD_LED_BLUE,  b ? LOW : HIGH);
}

void set_tft_brightness(uint8_t brightness) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(CYD_TFT_BL, brightness);
#else
    ledcWrite(0, brightness);
#endif
}

void init_backlight() {
    pinMode(CYD_TFT_BL, OUTPUT);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(CYD_TFT_BL, 5000, 8);
    ledcWrite(CYD_TFT_BL, 255);
#else
    ledcSetup(0, 5000, 8);
    ledcAttachPin(CYD_TFT_BL, 0);
    ledcWrite(0, 255);
#endif
}

// ----------------------------------------------------------------------------
// CYD Touch Calibration & Driver (XPT2046 Software SPI with NVS Storage)
// ----------------------------------------------------------------------------
Preferences touch_prefs;

// Standard calibration targets:
// Target 1: Screen (30, 30)
// Target 2: Screen (290, 30)
// Target 3: Screen (160, 210)
// Tested defaults for CYD 2.8" Landscape:
uint16_t cal_x0 = 3650;  // Raw reading at screen x = 30
uint16_t cal_x1 = 450;   // Raw reading at screen x = 290
uint16_t cal_y0 = 3650;  // Raw reading at screen y = 30
uint16_t cal_y1 = 450;   // Raw reading at screen y = 210
bool cal_swap_xy = true; // In CYD Landscape (Rotation 1), X and Y ADC channels are transposed
bool is_calibrated = false;

void init_cyd_touch() {
    // Crucial: De-assert MicroSD card CS (GPIO 5) to prevent SPI bus contention
    pinMode(CYD_SD_CS, OUTPUT);
    digitalWrite(CYD_SD_CS, HIGH);

    pinMode(XPT_CLK, OUTPUT);
    pinMode(XPT_MOSI, OUTPUT);
    pinMode(XPT_MISO, INPUT);
    pinMode(XPT_CS, OUTPUT);
    pinMode(XPT_IRQ, INPUT);
    digitalWrite(XPT_CS, HIGH);
    digitalWrite(XPT_CLK, LOW);

    // Load calibration from ESP32 Preferences NVS
    touch_prefs.begin("cyd_touch", false);
    if (touch_prefs.getBool("cal_done", false)) {
        cal_x0 = touch_prefs.getUShort("x0", 3650);
        cal_x1 = touch_prefs.getUShort("x1", 450);
        cal_y0 = touch_prefs.getUShort("y0", 3650);
        cal_y1 = touch_prefs.getUShort("y1", 450);
        cal_swap_xy = touch_prefs.getBool("swap", true);
        is_calibrated = true;
        Serial.printf("[TOUCH] Loaded NVS Calibration: x0=%d, x1=%d, y0=%d, y1=%d, swap=%d\n",
                      cal_x0, cal_x1, cal_y0, cal_y1, cal_swap_xy);
    } else {
        Serial.println("[TOUCH] Using verified default CYD Landscape touch calibration.");
    }
    touch_prefs.end();
}

// Low-level XPT2046 12-bit SPI Channel Read with accurate dummy conversion clock & 6us settling
static uint16_t xpt_read_channel(uint8_t cmd) {
    // 1. Clock out 8-bit command byte (MSB first)
    for (int i = 7; i >= 0; i--) {
        digitalWrite(XPT_MOSI, (cmd >> i) & 1);
        digitalWrite(XPT_CLK, HIGH);
        delayMicroseconds(6);
        digitalWrite(XPT_CLK, LOW);
        delayMicroseconds(6);
    }

    // 2. Clock 9: Mandatory dummy conversion clock (BUSY bit is LOW on MISO)
    digitalWrite(XPT_CLK, HIGH);
    delayMicroseconds(6);
    digitalWrite(XPT_CLK, LOW);
    delayMicroseconds(6);

    // 3. Clocks 10..21: Read true 12-bit ADC data (D11 down to D0)
    uint16_t result = 0;
    for (int i = 11; i >= 0; i--) {
        digitalWrite(XPT_CLK, HIGH);
        delayMicroseconds(6);
        if (digitalRead(XPT_MISO)) {
            result |= (1 << i);
        }
        digitalWrite(XPT_CLK, LOW);
        delayMicroseconds(6);
    }
    return result;
}

// Multi-sampled raw reading with spike rejection
bool get_raw_touch(uint16_t* raw_x, uint16_t* raw_y) {
    if (digitalRead(XPT_IRQ) != LOW) return false;

    digitalWrite(XPT_CS, LOW);
    delayMicroseconds(6);

    uint32_t sum_x = 0, sum_y = 0;
    int samples = 0;

    for (int s = 0; s < 5; s++) {
        uint16_t rx = xpt_read_channel(0xD0); // X-Channel
        uint16_t ry = xpt_read_channel(0x90); // Y-Channel
        if (rx > 150 && rx < 4000 && ry > 150 && ry < 4000) {
            sum_x += rx;
            sum_y += ry;
            samples++;
        }
        delayMicroseconds(10);
    }

    digitalWrite(XPT_CS, HIGH);

    if (samples < 2) return false;

    *raw_x = sum_x / samples;
    *raw_y = sum_y / samples;
    return true;
}

// Map calibrated raw touch to 320x240 screen pixels (Mathematically exact 30..290 / 30..210 range)
bool touchRead(uint16_t* px, uint16_t* py) {
    uint16_t rx = 0, ry = 0;
    if (!get_raw_touch(&rx, &ry)) return false;

    uint16_t x_adc = cal_swap_xy ? ry : rx;
    uint16_t y_adc = cal_swap_xy ? rx : ry;

    int screen_x = map(x_adc, cal_x0, cal_x1, 30, 290);
    int screen_y = map(y_adc, cal_y0, cal_y1, 30, 210);

    *px = (uint16_t)constrain(screen_x, 0, 319);
    *py = (uint16_t)constrain(screen_y, 0, 239);

    Serial.printf("[TOUCH] Raw ADC: (X=%d, Y=%d) -> Screen Pixel: (%d, %d)\n", rx, ry, *px, *py);
    return true;
}

// Helper to draw a calibration crosshair target
void draw_cal_target(int x, int y, uint16_t color) {
    tft.drawCircle(x, y, 12, color);
    tft.drawCircle(x, y, 4, color);
    tft.drawFastHLine(x - 16, y, 33, color);
    tft.drawFastVLine(x, y - 16, 33, color);
}

// Interactive 3-Point Calibration Screen
void run_touch_calibration() {
    Serial.println("[TOUCH] Starting Interactive Screen Calibration...");
    tft.fillScreen(C_BG);

    // Header banner
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_TEXT_HI, C_BG);
    tft.drawString("TOUCH CALIBRATION", 160, 60, 2);

    tft.setTextColor(C_TEXT_MID, C_BG);
    tft.drawString("Tap center of each crosshair with stylus/finger.", 160, 85, 1);

    // Target positions: (30, 30), (290, 30), (160, 210)
    const int targets_x[3] = {30, 290, 160};
    const int targets_y[3] = {30, 30,  210};
    uint16_t pts_rx[3] = {0};
    uint16_t pts_ry[3] = {0};

    for (int t = 0; t < 3; t++) {
        char prompt[48];
        snprintf(prompt, sizeof(prompt), "Target %d of 3: Tap the target...", t + 1);
        tft.fillRect(40, 110, 240, 20, C_BG);
        tft.setTextColor(C_ACCENT, C_BG);
        tft.drawString(prompt, 160, 110, 1);

        draw_cal_target(targets_x[t], targets_y[t], C_DANGER);

        // Wait for touch down on this target
        uint16_t rx = 0, ry = 0;
        bool touch_detected = false;
        while (!touch_detected) {
            if (get_raw_touch(&rx, &ry)) {
                // Collect stable average over 150ms
                uint32_t ax = 0, ay = 0;
                int count = 0;
                unsigned long start = millis();
                while (millis() - start < 150) {
                    uint16_t tx, ty;
                    if (get_raw_touch(&tx, &ty)) {
                        ax += tx;
                        ay += ty;
                        count++;
                    }
                    delay(10);
                }
                if (count > 5) {
                    pts_rx[t] = ax / count;
                    pts_ry[t] = ay / count;
                    touch_detected = true;
                }
            }
            delay(20);
        }

        // Draw confirmation flash on the captured target
        draw_cal_target(targets_x[t], targets_y[t], C_OK);
        set_cyd_led(false, true, false); // Green confirmation flash
        delay(100);
        set_cyd_led(false, false, false);

        // Wait for user to release touch
        while (digitalRead(XPT_IRQ) == LOW) {
            delay(30);
        }
        delay(200);

        // Erase old target
        tft.fillRect(targets_x[t] - 18, targets_y[t] - 18, 37, 37, C_BG);
    }

    // Determine axis swap:
    // Moving horizontally from Target 1 (x:30) to Target 2 (x:290):
    int delta_rx = abs((int)pts_rx[1] - (int)pts_rx[0]);
    int delta_ry = abs((int)pts_ry[1] - (int)pts_ry[0]);

    cal_swap_xy = (delta_ry > delta_rx);

    if (cal_swap_xy) {
        cal_x0 = pts_ry[0];
        cal_x1 = pts_ry[1];
        cal_y0 = pts_rx[0];
        cal_y1 = pts_rx[2];
    } else {
        cal_x0 = pts_rx[0];
        cal_x1 = pts_rx[1];
        cal_y0 = pts_ry[0];
        cal_y1 = pts_ry[2];
    }

    // Save calibration permanently in Preferences NVS
    touch_prefs.begin("cyd_touch", false);
    touch_prefs.putUShort("x0", cal_x0);
    touch_prefs.putUShort("x1", cal_x1);
    touch_prefs.putUShort("y0", cal_y0);
    touch_prefs.putUShort("y1", cal_y1);
    touch_prefs.putBool("swap", cal_swap_xy);
    touch_prefs.putBool("cal_done", true);
    touch_prefs.end();

    is_calibrated = true;
    Serial.printf("[TOUCH] Saved Calibration: x0=%d, x1=%d, y0=%d, y1=%d, swap=%d\n",
                  cal_x0, cal_x1, cal_y0, cal_y1, cal_swap_xy);

    // Show completion screen
    tft.fillRoundRect(60, 90, 200, 60, 10, C_SURFACE);
    tft.drawRoundRect(60, 90, 200, 60, 10, C_OK);
    tft.setTextColor(C_OK, C_SURFACE);
    tft.drawString("CALIBRATION SAVED!", 160, 105, 2);
    tft.setTextColor(C_TEXT_HI, C_SURFACE);
    tft.drawString("Touch settings locked in NVS.", 160, 128, 1);
    delay(1200);
}

// ----------------------------------------------------------------------------
// Weather Description Parser
// ----------------------------------------------------------------------------
String get_weather_desc(int code) {
    switch (code) {
        case 0:  return "Clear sky";
        case 1:  return "Mainly clear";
        case 2:  return "Partly cloudy";
        case 3:  return "Overcast";
        case 45: case 48: return "Foggy";
        case 51: case 53: case 55: return "Light drizzle";
        case 61: case 63: case 65: return "Rain showers";
        case 71: case 73: case 75: return "Snowfall";
        case 80: case 81: case 82: return "Tropical rain";
        case 95: case 96: case 99: return "Thunderstorm";
        default: return "Scattered";
    }
}

const char* get_short_weather_code_name(int code) {
    switch (code) {
        case 0:  return "Clear";
        case 1:  return "Fair";
        case 2:  return "P.Cloud";
        case 3:  return "Overcast";
        case 45: case 48: return "Fog";
        case 51: case 53: case 55: return "Drizzle";
        case 61: case 63: case 65: return "Rain";
        case 71: case 73: case 75: return "Snow";
        case 80: case 81: case 82: return "Showers";
        case 95: case 96: case 99: return "Storm";
        default: return "Partly";
    }
}

// ----------------------------------------------------------------------------
// Parametric Weather Vector Icons (Spec v2.0 §13)
// ----------------------------------------------------------------------------
void draw_parametric_icon(int cx, int cy, int code, bool is_day, int r) {
    if (code == 0) { // Clear Sky
        if (is_day) {
            tft.fillCircle(cx, cy, (int)(r * 0.55f), C_WARN);
            for (int a = 0; a < 360; a += 45) {
                float rad = a * 0.0174533f;
                int x1 = cx + (int)(cos(rad) * (r * 0.72f));
                int y1 = cy + (int)(sin(rad) * (r * 0.72f));
                int x2 = cx + (int)(cos(rad) * (r * 0.98f));
                int y2 = cy + (int)(sin(rad) * (r * 0.98f));
                tft.drawLine(x1, y1, x2, y2, C_WARN);
            }
        } else {
            // Crescent Moon
            tft.fillCircle(cx, cy, (int)(r * 0.8f), C_TEXT_HI);
            tft.fillCircle(cx + (int)(r * 0.45f), cy - (int)(r * 0.25f), (int)(r * 0.65f), C_SURFACE);
        }
    } else if (code <= 3) { // Cloud / Partly Cloudy
        if (code < 3 && is_day) {
            tft.fillCircle(cx - (int)(r * 0.35f), cy - (int)(r * 0.35f), (int)(r * 0.45f), C_WARN);
        }
        // Modern 3-circle cloud with flat base
        tft.fillCircle(cx - (int)(r * 0.35f), cy + (int)(r * 0.1f), (int)(r * 0.38f), C_TEXT_MID);
        tft.fillCircle(cx, cy - (int)(r * 0.15f), (int)(r * 0.48f), C_TEXT_HI);
        tft.fillCircle(cx + (int)(r * 0.38f), cy + (int)(r * 0.1f), (int)(r * 0.35f), C_TEXT_MID);
        tft.fillRoundRect(cx - (int)(r * 0.55f), cy, (int)(r * 1.1f), (int)(r * 0.45f), 3, C_TEXT_HI);
    } else if (code >= 51 && code <= 82) { // Rain / Showers
        tft.fillCircle(cx - (int)(r * 0.35f), cy - (int)(r * 0.15f), (int)(r * 0.38f), C_TEXT_MID);
        tft.fillCircle(cx, cy - (int)(r * 0.35f), (int)(r * 0.48f), C_TEXT_HI);
        tft.fillCircle(cx + (int)(r * 0.38f), cy - (int)(r * 0.15f), (int)(r * 0.35f), C_TEXT_MID);
        tft.fillRoundRect(cx - (int)(r * 0.55f), cy - (int)(r * 0.2f), (int)(r * 1.1f), (int)(r * 0.45f), 3, C_TEXT_HI);
        // Raindrops
        tft.drawLine(cx - (int)(r * 0.3f), cy + (int)(r * 0.35f), cx - (int)(r * 0.45f), cy + (int)(r * 0.75f), C_ACCENT);
        tft.drawLine(cx, cy + (int)(r * 0.35f), cx - (int)(r * 0.15f), cy + (int)(r * 0.75f), C_ACCENT);
        tft.drawLine(cx + (int)(r * 0.3f), cy + (int)(r * 0.35f), cx + (int)(r * 0.15f), cy + (int)(r * 0.75f), C_ACCENT);
    } else if (code >= 95) { // Thunderstorm
        tft.fillCircle(cx - (int)(r * 0.35f), cy - (int)(r * 0.2f), (int)(r * 0.38f), C_TEXT_MID);
        tft.fillCircle(cx, cy - (int)(r * 0.4f), (int)(r * 0.48f), C_TEXT_HI);
        tft.fillCircle(cx + (int)(r * 0.38f), cy - (int)(r * 0.2f), (int)(r * 0.35f), C_TEXT_MID);
        tft.fillRoundRect(cx - (int)(r * 0.55f), cy - (int)(r * 0.25f), (int)(r * 1.1f), (int)(r * 0.45f), 3, C_TEXT_HI);
        // Lightning bolt
        tft.drawLine(cx - 2, cy + 2, cx + 4, cy + 2, C_WARN);
        tft.drawLine(cx + 4, cy + 2, cx - 1, cy + 7, C_WARN);
        tft.drawLine(cx - 1, cy + 7, cx + 2, cy + 7, C_WARN);
        tft.drawLine(cx + 2, cy + 7, cx - 4, cy + 12, C_WARN);
    }
}

// ----------------------------------------------------------------------------
// Weather Fetcher (Open-Meteo REST API)
// ----------------------------------------------------------------------------
bool fetch_weather_data() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WEATHER] WiFi not connected. Skipping fetch.");
        return false;
    }

    Serial.println("[WEATHER] Synchronizing live weather for Kotte...");
    weather_sync_in_progress = true;

    HTTPClient http;
    char url[512];
    snprintf(url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,precipitation,weather_code,wind_speed_10m,surface_pressure&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=%s",
        LOC_LAT, LOC_LON, LOC_TIMEZONE
    );

    http.begin(url);
    http.setTimeout(8000);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        http.end();

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            weather.temp_c           = doc["current"]["temperature_2m"] | 28.5f;
            weather.humidity         = doc["current"]["relative_humidity_2m"] | 68;
            weather.feels_like_c     = doc["current"]["apparent_temperature"] | 31.0f;
            weather.is_day           = (doc["current"]["is_day"] | 1) == 1;
            weather.precipitation_mm = doc["current"]["precipitation"] | 0.0f;
            weather.weather_code     = doc["current"]["weather_code"] | 2;
            weather.wind_speed_kmh   = doc["current"]["wind_speed_10m"] | 12.0f;
            weather.gust_kmh         = weather.wind_speed_kmh * 1.5f;
            weather.pressure_hpa     = doc["current"]["surface_pressure"] | 1012.0f;
            weather.condition_text   = get_weather_desc(weather.weather_code);

            for (int i = 0; i < 4; i++) {
                weather.daily_max[i]  = doc["daily"]["temperature_2m_max"][i] | 30.0f;
                weather.daily_min[i]  = doc["daily"]["temperature_2m_min"][i] | 22.0f;
                weather.daily_code[i] = doc["daily"]["weather_code"][i] | 2;
            }

            weather.valid = true;
            last_weather_sync = millis();
            weather_sync_in_progress = false;
            weather_data_updated = true;

            Serial.printf("[WEATHER] Synced: %.1f C, %d%% RH, %s\n",
                          weather.temp_c, weather.humidity, weather.condition_text.c_str());

            // Update AI Agent Briefing & Insights
            if (weather.weather_code >= 95) {
                snprintf(brief_line1, sizeof(brief_line1), "STORM ALERT:");
                snprintf(brief_line2, sizeof(brief_line2), "Heavy rain & lightning");
                snprintf(brief_line3, sizeof(brief_line3), "Stay safe indoors");
            } else if (weather.precipitation_mm > 0.5f || weather.weather_code >= 61) {
                snprintf(brief_line1, sizeof(brief_line1), "RAIN EXPECTED:");
                snprintf(brief_line2, sizeof(brief_line2), "Carry a light umbrella");
                snprintf(brief_line3, sizeof(brief_line3), "Rain %.1fmm · RH %d%%", weather.precipitation_mm, weather.humidity);
            } else if (weather.temp_c >= 32.0f) {
                snprintf(brief_line1, sizeof(brief_line1), "HEAT ADVISORY:");
                snprintf(brief_line2, sizeof(brief_line2), "Hydrate & seek shade");
                snprintf(brief_line3, sizeof(brief_line3), "Feels like %.1f C", weather.feels_like_c);
            } else {
                snprintf(brief_line1, sizeof(brief_line1), "AI ADVISORY:");
                snprintf(brief_line2, sizeof(brief_line2), "Pleasant day in Kotte");
                snprintf(brief_line3, sizeof(brief_line3), "Wind %.0fkm/h · RH %d%%", weather.wind_speed_kmh, weather.humidity);
            }
            ai_is_speaking = true;

            return true;
        }
    }
    http.end();
    weather_sync_in_progress = false;
    return false;
}

// ----------------------------------------------------------------------------
// Eyes & Emotion Rendering (Spec v2.0 §6.6 - Sockets 40x36, Iris r:13, Pupil r:10)
// ----------------------------------------------------------------------------
void draw_eyes_v2(EyeEmotion emotion, float dx, float dy) {
    const int ex[2] = {30, 86};

    // Erase moving pupil cavity inside socket (34 x 30 box inside socket at y: 84)
    tft.fillRect(33, 87, 34, 30, C_SURFACE_ALT);
    tft.fillRect(89, 87, 34, 30, C_SURFACE_ALT);

    switch (emotion) {
        case EMO_BLINK:
            for (int i = 0; i < 2; i++) {
                tft.fillRoundRect(ex[i], 100, 40, 4, 2, C_ACCENT);
            }
            break;

        case EMO_SLEEP:
            for (int i = 0; i < 2; i++) {
                tft.fillRoundRect(ex[i], 100, 40, 3, 1, C_TEAL);
            }
            break;

        case EMO_HAPPY:
            // Joyful arches ^ ^
            for (int i = 0; i < 2; i++) {
                int cx = ex[i] + 20;
                for (int t = 0; t < 3; t++) {
                    tft.drawCircle(cx, 112 + t, 14, C_ACCENT);
                }
                tft.fillRect(ex[i], 108, 40, 12, C_SURFACE_ALT);
            }
            break;

        case EMO_THINKING:
            for (int i = 0; i < 2; i++) {
                static int scan_y = 2;
                scan_y = (scan_y + 2) % 32;
                tft.drawFastHLine(ex[i] + 4, 86 + scan_y, 32, C_ACCENT);
            }
            break;

        default: // EMO_NEUTRAL / Normal Tracking
            for (int i = 0; i < 2; i++) {
                int cx = (int)(ex[i] + 20 + dx);
                int cy = (int)(102 + dy);

                tft.drawCircle(cx, cy, 13, C_ACCENT);
                tft.fillCircle(cx, cy, 10, C_ACCENT);
                tft.fillCircle(cx - 3, cy - 3, 3, C_TEXT_HI);
            }
            break;
    }
}

// Mouth Equalizer (5 bars, uniform 9px pitch, baseline y: 134)
void draw_mouth_equalizer_v2(float level) {
    static const uint8_t EQ_X[5] = {58, 67, 76, 85, 94};
    const int EQ_BASE_Y = 134;

    // Erase equalizer band
    tft.fillRect(54, 126, 50, 16, C_SURFACE);

    for (int i = 0; i < 5; i++) {
        float h = 4.0f;
        if (i == 0 || i == 4) h = 4.0f + (level * 2.0f);
        else if (i == 1 || i == 3) h = 5.0f + (level * 4.0f);
        else if (i == 2) h = 6.0f + (level * 8.0f);

        int y = EQ_BASE_Y - (int)(h * 0.5f);
        tft.fillRoundRect(EQ_X[i], y, 5, (int)h, 2, C_ACCENT);
    }
}

// ----------------------------------------------------------------------------
// PAGE 0: MAIN DISPLAY CLOCK (Clean, Ambient Hero Clock)
// ----------------------------------------------------------------------------
void update_main_clock_digits(struct tm &timeinfo, bool force_all = false) {
    static int last_min = -1;
    static int last_sec = -1;
    static int last_day = -1;

    if (force_all) {
        last_min = -1;
        last_sec = -1;
        last_day = -1;
    }

    // 1. Time Digits HH:MM (Font 6, huge centered numbers)
    if (timeinfo.tm_min != last_min || force_all) {
        last_min = timeinfo.tm_min;
        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

        tft.fillRect(70, 44, 180, 50, C_SURFACE);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(C_TEXT_HI, C_SURFACE);
        tft.drawString(time_str, 160, 44, 6);
    }

    // 2. Seconds Progress Bar (160 x 4 px at y: 98, centered at x: 160)
    if (timeinfo.tm_sec != last_sec || force_all) {
        last_sec = timeinfo.tm_sec;
        int bar_w = (160 * timeinfo.tm_sec) / 60;
        if (timeinfo.tm_sec == 0) {
            tft.fillRoundRect(80, 98, 160, 4, 2, C_BORDER);
        } else {
            tft.fillRoundRect(80, 98, bar_w, 4, 2, C_ACCENT);
        }
    }

    // 3. Day & Date (Updated at midnight or force)
    if (timeinfo.tm_mday != last_day || force_all) {
        last_day = timeinfo.tm_mday;
        char date_str[48];
        strftime(date_str, sizeof(date_str), "%A · %d %B %Y", &timeinfo);
        for (int i = 0; date_str[i]; i++) date_str[i] = toupper(date_str[i]);

        tft.fillRect(30, 110, 260, 22, C_SURFACE);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(C_TEXT_HI, C_SURFACE);
        tft.drawString(date_str, 160, 110, 2);
    }
}

void render_clock_mode(struct tm &timeinfo) {
    tft.fillScreen(C_BG);

    // 1. Status Bar (8, 6, 304, 24, r: 12)
    tft.fillRoundRect(8, 6, 304, 24, 12, C_SURFACE);
    tft.drawRoundRect(8, 6, 304, 24, 12, C_BORDER);

    uint16_t dot_col = (WiFi.status() == WL_CONNECTED) ? C_OK : C_WARN;
    tft.fillCircle(22, 18, 4, dot_col);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_TEXT_HI, C_SURFACE);
    tft.drawString(LOCATION_NAME, 34, 14, 1);

    tft.fillRoundRect(132, 11, 72, 14, 7, C_SURFACE_ALT);
    tft.drawRoundRect(132, 11, 72, 14, 7, C_BORDER);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_TEAL, C_SURFACE_ALT);
    tft.drawString((WiFi.status() == WL_CONNECTED) ? "AI READY" : "OFFLINE", 168, 14, 1);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(C_ACCENT, C_SURFACE);
    tft.drawString("DESK CLOCK", 298, 14, 1);

    // 2. Hero Clock Container Card (8, 36, 304, 126, r: 12)
    tft.fillRoundRect(8, 36, 304, 126, 12, C_SURFACE);
    tft.drawRoundRect(8, 36, 304, 126, 12, C_BORDER);

    // Cyan Hero Rail
    tft.fillRoundRect(18, 46, 4, 106, 2, C_ACCENT);

    // Clock digits & date
    update_main_clock_digits(timeinfo, true);

    // Timezone Line
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_TEXT_MID, C_SURFACE);
    tft.drawString("SRI LANKA STANDARD TIME · UTC+05:30", 160, 134, 1);

    // 3. Bottom Weather Glance Card (8, 168, 304, 46, r: 10)
    tft.fillRoundRect(8, 168, 304, 46, 10, C_SURFACE);
    tft.drawRoundRect(8, 168, 304, 46, 10, C_BORDER);

    // Vector weather icon
    draw_parametric_icon(32, 191, weather.weather_code, weather.is_day, 11);

    // Temperature
    char t_buf[8];
    snprintf(t_buf, sizeof(t_buf), "%.1f", weather.temp_c);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_TEXT_HI, C_SURFACE);
    tft.drawString(t_buf, 52, 176, 4);

    tft.setTextColor(C_ACCENT, C_SURFACE);
    tft.drawString("°C", 102, 180, 2);

    // Divider
    tft.drawFastVLine(128, 174, 34, C_BORDER);

    // Condition & Range
    tft.setTextColor(C_TEAL, C_SURFACE);
    tft.drawString(weather.condition_text, 138, 176, 1);

    char hl_buf[32];
    snprintf(hl_buf, sizeof(hl_buf), "H %.0f°  L %.0f° · RH %d%%", weather.daily_max[0], weather.daily_min[0], weather.humidity);
    tft.setTextColor(C_TEXT_MID, C_SURFACE);
    tft.drawString(hl_buf, 138, 192, 1);

    // Action Pill
    tft.fillRoundRect(244, 176, 56, 26, 6, C_SURFACE_ALT);
    tft.drawRoundRect(244, 176, 56, 26, 6, C_BORDER);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_ACCENT, C_SURFACE_ALT);
    tft.drawString("DETAILS", 272, 184, 1);

    // 4. Page Indicator Dots & Navigation Hint (y: 220..236)
    tft.fillCircle(150, 224, 3, C_ACCENT);
    tft.drawCircle(160, 224, 2, C_BORDER);
    tft.drawCircle(170, 224, 2, C_BORDER);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_TEXT_LOW, C_BG);
    tft.drawString("TAP SCREEN FOR WEATHER & AI COMPANION", 160, 230, 1);
}

// ----------------------------------------------------------------------------
// PAGE 1: CLOCK WITH TODAY WEATHER & AI DETAILS (Companion Screen)
// ----------------------------------------------------------------------------
void update_companion_clock_banner(struct tm &timeinfo, bool force_all = false) {
    static int last_min = -1;
    static int last_sec = -1;

    if (force_all) {
        last_min = -1;
        last_sec = -1;
    }

    // Time HH:MM in Font 4
    if (timeinfo.tm_min != last_min || force_all) {
        last_min = timeinfo.tm_min;
        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

        tft.fillRect(26, 14, 62, 28, C_SURFACE);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(C_TEXT_HI, C_SURFACE);
        tft.drawString(time_str, 26, 14, 4);
    }

    // Seconds :SS in Font 2
    if (timeinfo.tm_sec != last_sec || force_all) {
        last_sec = timeinfo.tm_sec;
        char sec_str[8];
        snprintf(sec_str, sizeof(sec_str), ":%02d", timeinfo.tm_sec);

        tft.fillRect(92, 20, 36, 18, C_SURFACE);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(C_ACCENT, C_SURFACE);
        tft.drawString(sec_str, 92, 20, 2);
    }
}

void draw_today_weather_card() {
    // Upper Card (162, 60, 150, 72, r: 10)
    tft.fillRoundRect(162, 60, 150, 72, 10, C_SURFACE);
    tft.drawRoundRect(162, 60, 150, 72, 10, C_BORDER);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_TEXT_MID, C_SURFACE);
    tft.drawString("TODAY WEATHER", 172, 68, 1);

    tft.fillCircle(298, 72, 3, C_OK); // Live indicator

    draw_parametric_icon(280, 82, weather.weather_code, weather.is_day, 11);

    char t_buf[8];
    snprintf(t_buf, sizeof(t_buf), "%.1f", weather.temp_c);
    tft.setTextColor(C_TEXT_HI, C_SURFACE);
    tft.drawString(t_buf, 172, 80, 4);

    tft.setTextColor(C_ACCENT, C_SURFACE);
    tft.drawString("°C", 224, 84, 2);

    tft.setTextColor(C_TEAL, C_SURFACE);
    tft.drawString(weather.condition_text, 172, 108, 1);

    char hl_buf[24];
    snprintf(hl_buf, sizeof(hl_buf), "H %.0f°   L %.0f°", weather.daily_max[0], weather.daily_min[0]);
    tft.setTextColor(C_TEXT_MID, C_SURFACE);
    tft.drawString(hl_buf, 172, 120, 1);
}

void start_ai_story() {
    story_step = 0;
    story_is_running = true;
    last_story_step_time = millis();
    ai_is_speaking = true;
    current_emotion = EMO_HAPPY;
    draw_text_story_viewer();
}

void draw_text_story_viewer() {
    // Text Story Viewer inside Left Column below the mouth (12, 140, 140, 70, r: 6)
    tft.fillRoundRect(12, 140, 140, 70, 6, C_SURFACE_ALT);
    tft.drawRoundRect(12, 140, 140, 70, 6, C_BORDER);

    // Header (y: 144)
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_ACCENT, C_SURFACE_ALT);
    tft.drawString("🎙 AI STORY", 16, 144, 1);

    tft.setTextDatum(TR_DATUM);
    if (story_is_running) {
        char step_tag[12];
        snprintf(step_tag, sizeof(step_tag), "[%d/5]", story_step + 1);
        tft.setTextColor(C_WARN, C_SURFACE_ALT);
        tft.drawString(step_tag, 148, 144, 1);
    } else {
        tft.setTextColor(C_OK, C_SURFACE_ALT);
        tft.drawString("[END]", 148, 144, 1);
    }

    tft.drawFastHLine(14, 153, 136, C_BORDER);

    tft.setTextDatum(TL_DATUM);
    char l1[32] = "";
    char l2[32] = "";
    char l3[32] = "";
    char l4[32] = "";
    char l5[32] = "";

    uint16_t c1 = C_WARN;
    uint16_t c2 = C_TEXT_HI;
    uint16_t c3 = C_TEXT_MID;
    uint16_t c4 = C_TEAL;
    uint16_t c5 = C_TEXT_LOW;

    switch (story_step) {
        case 0: // Step 0: Today's Weather Overview
            c1 = C_WARN;
            strncpy(l1, "TODAY'S WEATHER:", sizeof(l1));
            snprintf(l2, sizeof(l2), "%.1f°C · %s", weather.temp_c, get_short_weather_code_name(weather.weather_code));
            snprintf(l3, sizeof(l3), "High %.0f° · Low %.0f°", weather.daily_max[0], weather.daily_min[0]);
            strncpy(l4, "Kotte, Sri Lanka", sizeof(l4));
            strncpy(l5, "Tropical climate", sizeof(l5));
            break;

        case 1: // Step 1: Atmosphere & Comfort
            c1 = C_WARN;
            strncpy(l1, "ATMOSPHERE & FEELS:", sizeof(l1));
            snprintf(l2, sizeof(l2), "Feels like: %.1f°C", weather.feels_like_c);
            snprintf(l3, sizeof(l3), "RH %d%% · %.0fhPa", weather.humidity, weather.pressure_hpa);
            snprintf(l4, sizeof(l4), "Wind %.0fkm/h (G %.0f)", weather.wind_speed_kmh, weather.gust_kmh);
            strncpy(l5, "Barometric: Stable", sizeof(l5));
            break;

        case 2: // Step 2: Weather & UV Advisory
            c1 = C_WARN;
            strncpy(l1, "WEATHER ADVISORY:", sizeof(l1));
            snprintf(l2, sizeof(l2), "UV Index: %.1f (%s)", weather.uv_index, (weather.uv_index >= 6.0f) ? "High" : "Mod");
            if (weather.precipitation_mm > 0.2f || weather.weather_code >= 61) {
                strncpy(l3, "Rain expected today", sizeof(l3));
                strncpy(l4, "Carry an umbrella!", sizeof(l4));
            } else if (weather.temp_c >= 31.0f) {
                strncpy(l3, "Warm midday sun", sizeof(l3));
                strncpy(l4, "Wear sun protection", sizeof(l4));
            } else {
                strncpy(l3, "Pleasant air quality", sizeof(l3));
                strncpy(l4, "Great outdoor day", sizeof(l4));
            }
            strncpy(l5, "Advisory synced", sizeof(l5));
            break;

        case 3: // Step 3: Forecast of Weather (Tomorrow & Next Days)
            c1 = C_WARN;
            strncpy(l1, "WEATHER FORECAST:", sizeof(l1));
            snprintf(l2, sizeof(l2), "+1D: %.0f/%.0f° %s", weather.daily_max[1], weather.daily_min[1], get_short_weather_code_name(weather.daily_code[1]));
            snprintf(l3, sizeof(l3), "+2D: %.0f/%.0f° %s", weather.daily_max[2], weather.daily_min[2], get_short_weather_code_name(weather.daily_code[2]));
            snprintf(l4, sizeof(l4), "+3D: %.0f/%.0f° %s", weather.daily_max[3], weather.daily_min[3], get_short_weather_code_name(weather.daily_code[3]));
            strncpy(l5, "Synoptic trends synced", sizeof(l5));
            break;

        case 4: // Step 4: Synoptic Summary & Weekly Outlook
            c1 = C_WARN;
            strncpy(l1, "SYNOPTIC OUTLOOK:", sizeof(l1));
            if (weather.daily_code[1] >= 51 || weather.daily_code[2] >= 51 || weather.daily_code[3] >= 51) {
                strncpy(l2, "Mid-week rain incoming", sizeof(l2));
            } else {
                strncpy(l2, "Stable sunshine ahead", sizeof(l2));
            }
            snprintf(l3, sizeof(l3), "Week peak: %.0f°C", max(weather.daily_max[0], max(weather.daily_max[1], weather.daily_max[2])));
            strncpy(l4, "Hydrate & stay cool!", sizeof(l4));
            strncpy(l5, "Concluding report...", sizeof(l5));
            break;

        case 5: // Step 5: Finished — Explicitly Says "END"
        default:
            c1 = C_OK;
            c2 = C_WARN;
            c3 = C_TEXT_HI;
            c4 = C_TEXT_MID;
            c5 = C_ACCENT;
            strncpy(l1, "BRIEFING COMPLETE:", sizeof(l1));
            strncpy(l2, "— END —", sizeof(l2));
            strncpy(l3, "Weather briefing done", sizeof(l3));
            strncpy(l4, "AI Agent on standby", sizeof(l4));
            strncpy(l5, "[Tap face to replay]", sizeof(l5));
            break;
    }

    tft.setTextColor(c1, C_SURFACE_ALT);
    tft.drawString(l1, 16, 157, 1);

    if (story_step == 5) {
        // Render "— END —" with high emphasis in Font 2 amber
        tft.setTextColor(C_WARN, C_SURFACE_ALT);
        tft.drawString(l2, 16, 168, 2);
        tft.setTextColor(c3, C_SURFACE_ALT);
        tft.drawString(l3, 16, 184, 1);
        tft.setTextColor(c4, C_SURFACE_ALT);
        tft.drawString(l4, 16, 194, 1);
        tft.setTextColor(c5, C_SURFACE_ALT);
        tft.drawString(l5, 16, 203, 1);
    } else {
        tft.setTextColor(c2, C_SURFACE_ALT);
        tft.drawString(l2, 16, 168, 1);
        tft.setTextColor(c3, C_SURFACE_ALT);
        tft.drawString(l3, 16, 179, 1);
        tft.setTextColor(c4, C_SURFACE_ALT);
        tft.drawString(l4, 16, 190, 1);
        tft.setTextColor(c5, C_SURFACE_ALT);
        tft.drawString(l5, 16, 201, 1);
    }
}

void draw_atmosphere_advisory_card() {
    // Right Column Lower: Atmospheric Telemetry & Advisory (162, 136, 150, 78, r: 10)
    tft.fillRoundRect(162, 136, 150, 78, 10, C_SURFACE_ALT);
    tft.drawRoundRect(162, 136, 150, 78, 10, C_BORDER);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_ACCENT, C_SURFACE_ALT);
    tft.drawString("⚡ ATMOSPHERE", 172, 142, 1);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(C_OK, C_SURFACE_ALT);
    tft.drawString("LIVE", 302, 142, 1);

    tft.drawFastHLine(170, 152, 134, C_BORDER);

    tft.setTextDatum(TL_DATUM);
    char buf[32];

    // Line 1: Feels like & UV Index
    snprintf(buf, sizeof(buf), "Feels %.0f° · UV %.1f", weather.feels_like_c, weather.uv_index);
    tft.setTextColor(C_TEXT_HI, C_SURFACE_ALT);
    tft.drawString(buf, 172, 156, 1);

    // Line 2: Humidity & Pressure
    snprintf(buf, sizeof(buf), "RH %d%% · %.0fhPa", weather.humidity, weather.pressure_hpa);
    tft.setTextColor(C_TEAL, C_SURFACE_ALT);
    tft.drawString(buf, 172, 168, 1);

    // Line 3: Wind speed & gusts
    snprintf(buf, sizeof(buf), "Wind %.0fkm/h (G %.0f)", weather.wind_speed_kmh, weather.gust_kmh);
    tft.setTextColor(C_TEXT_MID, C_SURFACE_ALT);
    tft.drawString(buf, 172, 180, 1);

    // Line 4: Action Advisory
    tft.setTextColor(C_WARN, C_SURFACE_ALT);
    tft.drawString(brief_line2, 172, 192, 1);

    // Line 5: Rain & Air glance
    snprintf(buf, sizeof(buf), "Rain: %.1fmm · Air: OK", weather.precipitation_mm);
    tft.setTextColor(C_TEXT_LOW, C_SURFACE_ALT);
    tft.drawString(buf, 172, 204, 1);
}

void render_companion_mode(struct tm &timeinfo) {
    tft.fillScreen(C_BG);

    // 1. Top Clock Banner (8, 6, 304, 48, r: 10)
    tft.fillRoundRect(8, 6, 304, 48, 10, C_SURFACE);
    tft.drawRoundRect(8, 6, 304, 48, 10, C_BORDER);

    tft.fillRoundRect(16, 12, 3, 36, 1, C_ACCENT);

    update_companion_clock_banner(timeinfo, true);

    tft.drawFastVLine(132, 12, 36, C_BORDER);

    char date_short[24];
    strftime(date_short, sizeof(date_short), "%a · %d %b", &timeinfo);
    for (int i = 0; date_short[i]; i++) date_short[i] = toupper(date_short[i]);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_TEXT_HI, C_SURFACE);
    tft.drawString(date_short, 142, 12, 2);

    tft.setTextColor(C_TEAL, C_SURFACE);
    tft.drawString("KOTTE, LK · AI READY", 142, 32, 1);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(C_ACCENT, C_SURFACE);
    tft.drawString("[FORECAST >]", 298, 20, 1);

    // 2. Left Column: AI Companion Card (8, 60, 148, 154, r: 10)
    tft.fillRoundRect(8, 60, 148, 154, 10, C_SURFACE);
    tft.drawRoundRect(8, 60, 148, 154, 10, C_BORDER);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_ACCENT, C_SURFACE);
    tft.drawString("AI COMPANION", 18, 68, 1);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(C_OK, C_SURFACE);
    tft.drawString("● ACTIVE", 146, 68, 1);

    tft.drawFastHLine(16, 78, 132, C_BORDER);

    // Eye sockets (40 x 36, r: 10)
    tft.fillRoundRect(30, 84, 40, 36, 10, C_SURFACE_ALT);
    tft.drawRoundRect(30, 84, 40, 36, 10, C_BORDER);

    tft.fillRoundRect(86, 84, 40, 36, 10, C_SURFACE_ALT);
    tft.drawRoundRect(86, 84, 40, 36, 10, C_BORDER);

    draw_eyes_v2(current_emotion, 0.0f, 0.0f);
    draw_mouth_equalizer_v2(0.0f);

    // Text Story Viewer replaces tap to talk and all below items!
    draw_text_story_viewer();

    // 3. Right Column: Today Weather Card & Atmosphere Advisory Card
    draw_today_weather_card();
    draw_atmosphere_advisory_card();

    // 4. Page Indicator Dots & Navigation Hint (y: 220..236)
    tft.drawCircle(150, 224, 2, C_BORDER);
    tft.fillCircle(160, 224, 3, C_ACCENT);
    tft.drawCircle(170, 224, 2, C_BORDER);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_TEXT_LOW, C_BG);
    tft.drawString("TAP SCREEN FOR 4-DAY FORECAST", 160, 230, 1);
}

// ----------------------------------------------------------------------------
// PAGE 2: 4-DAY FORECAST ANALYZER
// ----------------------------------------------------------------------------
void render_forecast_mode(struct tm &timeinfo) {
    tft.fillScreen(C_BG);

    // 1. Header Bar (8, 6, 304, 26, r: 10)
    tft.fillRoundRect(8, 6, 304, 26, 10, C_SURFACE);
    tft.drawRoundRect(8, 6, 304, 26, 10, C_BORDER);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_TEXT_HI, C_SURFACE);
    tft.drawString("FORECAST ANALYZER", 18, 11, 2);

    tft.setTextColor(C_TEXT_MID, C_SURFACE);
    tft.drawString("4-DAY SYNOPTIC", 176, 13, 1);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(C_WARN, C_SURFACE);
    tft.drawString("[ < MAIN CLOCK ]", 298, 11, 1);

    // 2. Dual Current Metrics Cards (y: 36..120, h: 84)
    // Left Hero Card (8, 36, 150, 84, r: 10)
    tft.fillRoundRect(8, 36, 150, 84, 10, C_SURFACE);
    tft.drawRoundRect(8, 36, 150, 84, 10, C_BORDER);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_TEXT_MID, C_SURFACE);
    tft.drawString("NOW", 18, 42, 1);

    char t_buf[8];
    snprintf(t_buf, sizeof(t_buf), "%.1f", weather.temp_c);
    tft.setTextColor(C_TEXT_HI, C_SURFACE);
    tft.drawString(t_buf, 18, 54, 4);

    tft.setTextColor(C_ACCENT, C_SURFACE);
    tft.drawString("°C", 70, 58, 2);

    draw_parametric_icon(128, 62, weather.weather_code, weather.is_day, 12);

    tft.setTextColor(C_TEAL, C_SURFACE);
    tft.drawString(weather.condition_text, 18, 86, 1);

    char hl_buf[24];
    snprintf(hl_buf, sizeof(hl_buf), "H %.0f°   L %.0f°", weather.daily_max[0], weather.daily_min[0]);
    tft.setTextColor(C_TEXT_MID, C_SURFACE);
    tft.drawString(hl_buf, 18, 100, 1);

    // Right Atmosphere Card (164, 36, 148, 84, r: 10)
    tft.fillRoundRect(164, 36, 148, 84, 10, C_SURFACE);
    tft.drawRoundRect(164, 36, 148, 84, 10, C_BORDER);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_TEXT_MID, C_SURFACE);
    tft.drawString("ATMOSPHERE", 174, 42, 1);

    auto draw_row = [](int y, const char* label, const char* val) {
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(C_TEXT_MID, C_SURFACE);
        tft.drawString(label, 174, y, 1);

        tft.setTextDatum(TR_DATUM);
        tft.setTextColor(C_TEXT_HI, C_SURFACE);
        tft.drawString(val, 300, y, 1);
    };

    char hum_buf[16], wind_buf[16], pres_buf[16], rain_buf[16];
    snprintf(hum_buf, sizeof(hum_buf), "%d %%", weather.humidity);
    snprintf(wind_buf, sizeof(wind_buf), "%.0f km/h", weather.wind_speed_kmh);
    snprintf(pres_buf, sizeof(pres_buf), "%.0f hPa", weather.pressure_hpa);
    snprintf(rain_buf, sizeof(rain_buf), "%.1f mm", weather.precipitation_mm);

    draw_row(56, "HUMIDITY", hum_buf);
    draw_row(70, "PRESSURE", pres_buf);
    draw_row(84, "WIND SPD", wind_buf);
    draw_row(98, "RAIN MM", rain_buf);

    // 3. 4-Day Forecast Strip (8, 126, 304, 66, r: 8)
    tft.fillRoundRect(8, 126, 304, 66, 8, C_SURFACE);
    tft.drawRoundRect(8, 126, 304, 66, 8, C_BORDER);

    const int FC_CX[4] = {46, 122, 198, 274};
    const char* days[4] = {"TODAY", "+1 DAY", "+2 DAYS", "+3 DAYS"};

    for (int i = 0; i < 4; i++) {
        if (i < 3) tft.drawFastVLine(84 + (i * 76), 132, 54, C_BORDER);

        tft.setTextDatum(TC_DATUM);
        tft.setTextColor((i == 0) ? C_ACCENT : C_TEXT_MID, C_SURFACE);
        tft.drawString(days[i], FC_CX[i], 132, 1);

        draw_parametric_icon(FC_CX[i], 152, weather.daily_code[i], true, 10);

        char fc_hl[16];
        snprintf(fc_hl, sizeof(fc_hl), "%.0f/%.0f°", weather.daily_max[i], weather.daily_min[i]);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(C_TEXT_HI, C_SURFACE);
        tft.drawString(fc_hl, FC_CX[i], 168, 2);
    }

    // 4. AI Forecast Advisory Strip (8, 198, 304, 20, r: 8)
    tft.fillRoundRect(8, 198, 304, 20, 8, C_SURFACE);
    tft.drawRoundRect(8, 198, 304, 20, 8, C_BORDER);

    tft.fillCircle(20, 208, 3, C_ACCENT);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(C_TEXT_HI, C_SURFACE);
    tft.drawString("AI: LOW PRECIPITATION · STABLE UV · CARRY SHADE", 28, 204, 1);

    // 5. Page Indicator Dots & Navigation Hint (y: 224..238)
    tft.drawCircle(150, 226, 2, C_BORDER);
    tft.drawCircle(160, 226, 2, C_BORDER);
    tft.fillCircle(170, 226, 3, C_ACCENT);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_TEXT_LOW, C_BG);
    tft.drawString("TAP SCREEN TO RETURN TO MAIN CLOCK", 160, 232, 1);
}

// ----------------------------------------------------------------------------
// MODE C: NIGHT CLOCK (Warm Amber Minimalist Bedtime Mode)
// ----------------------------------------------------------------------------
void update_night_clock_digits(struct tm &timeinfo, bool force_all = false) {
    static int last_night_min = -1;
    static int last_night_sec = -1;

    // 1. Time Digits HH:MM (Updated once per minute or force)
    if (timeinfo.tm_min != last_night_min || force_all) {
        last_night_min = timeinfo.tm_min;
        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

        tft.fillRect(40, 52, 240, 50, C_BLACK);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(C_NIGHT_AMBER, C_BLACK);
        tft.drawString(time_str, 160, 52, 6);
    }

    // 2. Seconds Progress Bar (100, 140, 120, 3, r: 2)
    if (timeinfo.tm_sec != last_night_sec || force_all) {
        last_night_sec = timeinfo.tm_sec;
        int bar_w = (120 * timeinfo.tm_sec) / 60;
        if (timeinfo.tm_sec == 0) {
            tft.fillRoundRect(100, 140, 120, 3, 1, C_NIGHT_FAINT);
        } else {
            tft.fillRoundRect(100, 140, bar_w, 3, 1, C_NIGHT_DIM);
        }
    }
}

void render_night_mode(struct tm &timeinfo) {
    tft.fillScreen(C_BLACK);
    set_tft_brightness(35); // Dimmed to 35 PWM for night room comfort

    // Day · Date String Centered at (160, 158)
    char date_buf[48];
    strftime(date_buf, sizeof(date_buf), "%A · %d %B", &timeinfo);
    for (int i = 0; date_buf[i]; i++) date_buf[i] = toupper(date_buf[i]);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_NIGHT_DIM, C_BLACK);
    tft.drawString(date_buf, 160, 158, 2);

    // Weather Telemetry Centered at (160, 186)
    char wx_buf[48];
    snprintf(wx_buf, sizeof(wx_buf), "%s · %.1f°C · %s", LOCATION_NAME, weather.temp_c, weather.condition_text.c_str());
    for (int i = 0; wx_buf[i]; i++) wx_buf[i] = toupper(wx_buf[i]);
    tft.drawString(wx_buf, 160, 186, 1);

    // Tap to Wake Hint Pill (116, 208, 88, 20, r: 10)
    tft.drawRoundRect(116, 208, 88, 20, 10, C_NIGHT_FAINT);
    tft.setTextColor(C_NIGHT_FAINT, C_BLACK);
    tft.drawString("TAP TO WAKE", 160, 214, 1);

    // Clock digits
    update_night_clock_digits(timeinfo, true);
}

// ----------------------------------------------------------------------------
// Mode Transition Helper (Flicker-Free with Backlight Fade)
// ----------------------------------------------------------------------------
void switch_mode(DisplayMode new_mode, struct tm &timeinfo) {
    current_mode = new_mode;
    if (new_mode == MODE_CLOCK) {
        story_is_running = false;
        ai_is_speaking = false;
        set_tft_brightness(255);
        render_clock_mode(timeinfo);
    } else if (new_mode == MODE_COMPANION) {
        set_tft_brightness(255);
        // Automatically start AI Story Teller when opening second page!
        story_step = 0;
        story_is_running = true;
        last_story_step_time = millis();
        ai_is_speaking = true;
        current_emotion = EMO_HAPPY;
        render_companion_mode(timeinfo);
    } else if (new_mode == MODE_FORECAST) {
        story_is_running = false;
        ai_is_speaking = false;
        set_tft_brightness(255);
        render_forecast_mode(timeinfo);
    } else if (new_mode == MODE_NIGHT) {
        story_is_running = false;
        ai_is_speaking = false;
        current_emotion = EMO_SLEEP;
        render_night_mode(timeinfo);
    }
}

// ----------------------------------------------------------------------------
// Secondary 128x64 OLED: HUD Telemetry
// ----------------------------------------------------------------------------
static void oled_text_right(const char* s, int x_right, int y) {
    int w = (int)strlen(s) * 6;
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(x_right - w, y);
    oled.print(s);
}

void update_oled_telemetry_hud() {
    if (!oled_available) return;

    oled.clearDisplay();

    // 1. Header (y: 0..10) - Inverse Bar
    oled.fillRect(0, 0, 128, 11, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(2, 1);
    oled.print("AI COMPANION");

    static int spin_frame = 0;
    const char spin_chars[] = {'|', '/', '-', '\\'};
    oled.setCursor(118, 1);
    oled.printf("[%c]", spin_chars[spin_frame % 4]);
    spin_frame++;

    // 2. CPU (y: 12..22)
    static int sim_cpu = 34;
    sim_cpu = constrain(sim_cpu + random(-3, 4), 16, 68);
    if (weather_sync_in_progress) sim_cpu = 88;

    cpu_hist[hist_head] = sim_cpu;
    hist_head = (hist_head + 1) % 26;

    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 13);
    oled.print("CPU");

    char cpu_str[8];
    snprintf(cpu_str, sizeof(cpu_str), "%d%%", sim_cpu);
    oled_text_right(cpu_str, 70, 13);

    // CPU Sparkline (74, 13, 52, 10)
    for (int i = 0; i < 25; i++) {
        int idx1 = (hist_head + i) % 26;
        int idx2 = (hist_head + i + 1) % 26;
        int y1 = 22 - (cpu_hist[idx1] * 9 / 100);
        int y2 = 22 - (cpu_hist[idx2] * 9 / 100);
        oled.drawLine(74 + (i * 2), y1, 74 + ((i + 1) * 2), y2, SSD1306_WHITE);
    }

    // 3. HEAP (y: 23..33)
    uint32_t free_heap = ESP.getFreeHeap();
    uint32_t total_heap = 327680;
    uint32_t used_heap = total_heap - min(total_heap, free_heap);

    oled.setCursor(0, 24);
    oled.print("HEAP");

    char heap_str[8];
    snprintf(heap_str, sizeof(heap_str), "%dK", used_heap / 1024);
    oled_text_right(heap_str, 70, 24);

    // HEAP Progress Bar (74, 25, 52, 7)
    oled.drawRect(74, 25, 52, 7, SSD1306_WHITE);
    int bar_fill = (used_heap * 48) / total_heap;
    if (bar_fill > 0) oled.fillRect(76, 27, bar_fill, 3, SSD1306_WHITE);

    // 4. NPU Activity Waveform (y: 34..44)
    oled.setCursor(0, 35);
    oled.print("NPU");
    oled_text_right(story_is_running ? "RUN" : "LIVE", 70, 35);

    // Animated Sine Wave (74..126, cy: 39)
    float phase = millis() * 0.006f;
    int prev_sy = -1;
    for (int x = 74; x <= 126; x += 2) {
        float freq = story_is_running ? 0.45f : 0.30f;
        float amp = story_is_running ? 4.0f : 2.5f;
        int sy = 39 + (int)(amp * sinf(phase + (x - 74) * freq));
        if (prev_sy >= 0) oled.drawLine(x - 2, prev_sy, x, sy, SSD1306_WHITE);
        prev_sy = sy;
    }

    // 5. TOPS Accelerator Telemetry (y: 46..55) - Completely replaces WiFi!
    oled.setCursor(0, 46);
    oled.print("TOPS");

    float tops_val = story_is_running ? 2.84f : (ai_is_speaking ? 2.65f : 1.42f);
    char tops_str[8];
    snprintf(tops_str, sizeof(tops_str), "%.2f", tops_val);
    oled_text_right(tops_str, 70, 46);

    // 6-Segment Accelerator Gauge (74, 46, 52, 8)
    int active_blocks = story_is_running ? 5 : (ai_is_speaking ? 4 : 2);
    if ((spin_frame % 4 == 0) && story_is_running) active_blocks = 6; // Dynamic pulse during speech
    for (int b = 0; b < 6; b++) {
        int bx = 74 + (b * 8);
        if (b < active_blocks) {
            oled.fillRect(bx, 46, 6, 7, SSD1306_WHITE);
        } else {
            oled.drawRect(bx, 46, 6, 7, SSD1306_WHITE);
        }
    }

    // 6. Footer (y: 56..63)
    oled.setCursor(0, 56);
    if (current_mode == MODE_NIGHT)          oled.print("NGT-AI");
    else if (current_mode == MODE_FORECAST)   oled.print("FC-AI");
    else if (current_mode == MODE_COMPANION) {
        if (story_is_running)                oled.print("STRY-AI");
        else                                 oled.print("CMP-AI");
    }
    else                                     oled.print("CLK-AI");

    if (story_is_running) {
        char st_step[12];
        snprintf(st_step, sizeof(st_step), "STORY %d/5", story_step + 1);
        oled_text_right(st_step, 126, 56);
    } else if (current_mode == MODE_COMPANION && story_step >= 5) {
        oled_text_right("END", 126, 56);
    } else {
        oled_text_right("NOMINAL", 126, 56);
    }

    oled.display();
}

// ----------------------------------------------------------------------------
// Schedule Checker
// ----------------------------------------------------------------------------
inline bool is_scheduled_night(const struct tm &timeinfo) {
    if (timeinfo.tm_year < 120) return false;
    int now_m = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    int night_s = NIGHT_START_H * 60 + NIGHT_START_M;
    int night_e = NIGHT_END_H * 60 + NIGHT_END_M;
    if (night_s <= night_e) return now_m >= night_s && now_m < night_e;
    return now_m >= night_s || now_m < night_e;
}

// ----------------------------------------------------------------------------
// FreeRTOS Background Weather Sync Task
// ----------------------------------------------------------------------------
void weather_sync_task(void *pvParameters) {
    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            if (!weather.valid || (millis() - last_weather_sync >= WEATHER_SYNC_INTERVAL_MS)) {
                fetch_weather_data();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// ----------------------------------------------------------------------------
// Setup Routine
// ----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n========================================================");
    Serial.println("  ESP32 CYD AI WEATHER COMPANION & ANALYZER v2.1       ");
    Serial.println("========================================================");

    // 1. Initialize CYD RGB LED
    pinMode(CYD_LED_RED,   OUTPUT);
    pinMode(CYD_LED_GREEN, OUTPUT);
    pinMode(CYD_LED_BLUE,  OUTPUT);
    set_cyd_led(false, false, true); // Subtle blue pulse

    // 2. Initialize Backlight
    init_backlight();

    // 3. Initialize Touchscreen & De-assert MicroSD CS (GPIO 5)
    init_cyd_touch();

    // 4. Initialize Primary TFT
    tft.init();
    tft.setRotation(1); // 320x240 Landscape
    tft.fillScreen(C_BG);

    // 5. Initialize Secondary OLED (I2C on IO27 / IO22)
    Wire.begin(OLED_SDA, OLED_SCL);
    if (oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR1) || oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR2)) {
        oled_available = true;
        Serial.println("[OLED] SSD1306 128x64 initialized.");
    }

    // 6. Professional Splash Screen
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(C_TEXT_HI, C_BG);
    tft.drawString("AI COMPANION", 160, 96, 2);

    tft.setTextColor(C_TEXT_MID, C_BG);
    tft.drawString("WEATHER CORE v2.1", 160, 120, 1);

    tft.fillRoundRect(96, 146, 128, 4, 2, C_BORDER);
    tft.fillRoundRect(96, 146, 40, 4, 2, C_ACCENT);

    tft.drawString("LINKING WIFI...", 160, 166, 1);

    // 7. Connect to WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long wifi_start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifi_start < 7000)) {
        delay(250);
    }

    tft.fillRoundRect(96, 146, 80, 4, 2, C_ACCENT);
    tft.fillRect(90, 164, 140, 16, C_BG);
    tft.drawString("SYNCING NTP...", 160, 166, 1);

    // 8. Configure NTP (GMT+5:30)
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET, NTP_SERVER1, NTP_SERVER2);

    tft.fillRoundRect(96, 146, 128, 4, 2, C_ACCENT);
    tft.fillRect(90, 164, 140, 16, C_BG);
    tft.drawString("FETCHING WEATHER...", 160, 166, 1);

    // 9. Initial Weather Fetch
    fetch_weather_data();

    // 10. Start FreeRTOS Background Sync Task on Core 0
    xTaskCreatePinnedToCore(
        weather_sync_task,
        "weather_task",
        4096,
        NULL,
        1,
        NULL,
        0
    );

    set_cyd_led(false, false, false);

    // 11. Initial Screen Render
    struct tm initial_time;
    if (!getLocalTime(&initial_time)) {
        initial_time.tm_hour = 12;
        initial_time.tm_min  = 0;
        initial_time.tm_sec  = 0;
        initial_time.tm_mday = 12;
        initial_time.tm_mon  = 9; // October
        initial_time.tm_year = 126;
        initial_time.tm_wday = 1;
    }

    // Default start mode: Main Display Clock (or night clock if scheduled)
    if (is_scheduled_night(initial_time)) {
        switch_mode(MODE_NIGHT, initial_time);
    } else {
        switch_mode(MODE_CLOCK, initial_time);
    }
}

// ----------------------------------------------------------------------------
// Main Loop (Core 1)
// ----------------------------------------------------------------------------
void loop() {
    unsigned long now = millis();

    // 0. Serial Command Listener (Type 'c' in Serial Monitor to Calibrate Touch)
    if (Serial.available()) {
        char ch = Serial.read();
        if (ch == 'c' || ch == 'C') {
            Serial.println("[TOUCH] Serial 'c' command received. Launching Touch Calibration...");
            struct tm tinfo;
            getLocalTime(&tinfo);
            run_touch_calibration();
            switch_mode(current_mode, tinfo);
            return;
        }
    }

    // 1. Fetch Local Time
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        timeinfo.tm_hour = 12;
        timeinfo.tm_min  = 0;
        timeinfo.tm_sec  = 0;
        timeinfo.tm_mday = 12;
        timeinfo.tm_mon  = 9;
        timeinfo.tm_year = 126;
        timeinfo.tm_wday = 1;
    }

    // 2. Day / Night Automatic Scheduler (Checked every second)
    static unsigned long last_sched_check = 0;
    if (now - last_sched_check >= 1000) {
        last_sched_check = now;
        bool night_time = is_scheduled_night(timeinfo);

        if (!manual_mode_override) {
            if (night_time && current_mode != MODE_NIGHT) {
                switch_mode(MODE_NIGHT, timeinfo);
            } else if (!night_time && current_mode == MODE_NIGHT) {
                switch_mode(MODE_CLOCK, timeinfo);
            }
        }
    }

    // 3. Touch Input Processing:
    // Long press 3.5s anywhere triggers calibration:
    static unsigned long touch_hold_start = 0;
    if (digitalRead(XPT_IRQ) == LOW) {
        if (touch_hold_start == 0) touch_hold_start = now;
        else if (now - touch_hold_start > 3500) {
            Serial.println("[TOUCH] 3.5s Screen Hold detected! Launching Touch Calibration...");
            touch_hold_start = 0;
            run_touch_calibration();
            switch_mode(current_mode, timeinfo);
            return;
        }
    } else {
        touch_hold_start = 0;
    }

    // Interactive Tap Navigation:
    static unsigned long last_touch_poll = 0;
    if (now - last_touch_poll >= 50) {
        last_touch_poll = now;
        uint16_t tx = 0, ty = 0;
        if (touchRead(&tx, &ty)) {
            Serial.printf("[TOUCH] Tap at (%d, %d) in Mode %d\n", tx, ty, (int)current_mode);
            manual_mode_override = true;

            // PAGE 3 (Night Mode): Any tap wakes to Main Clock
            if (current_mode == MODE_NIGHT) {
                switch_mode(MODE_CLOCK, timeinfo);
            }
            // PAGE 0 (Main Display Clock): Any tap -> Page 1 (Clock with Today Weather & AI Companion)
            else if (current_mode == MODE_CLOCK) {
                switch_mode(MODE_COMPANION, timeinfo);
            }
            // PAGE 1 (Clock with Today Weather & AI Details):
            else if (current_mode == MODE_COMPANION) {
                // If user tapped avatar face or text story viewer (x: 8..156, y: 60..214):
                if (tx >= 8 && tx <= 156 && ty >= 60 && ty <= 214) {
                    set_cyd_led(false, true, true); // Cyan LED pulse
                    start_ai_story();
                    draw_eyes_v2(EMO_HAPPY, 0.0f, 0.0f);
                    delay(120);
                    set_cyd_led(false, false, false);
                } else {
                    // Tap anywhere else on Page 1 -> Advances to Page 2 (Forecast Analyzer)
                    switch_mode(MODE_FORECAST, timeinfo);
                }
            }
            // PAGE 2 (Forecast Analyzer): Any tap -> Returns to Page 0 (Main Display Clock)
            else if (current_mode == MODE_FORECAST) {
                switch_mode(MODE_CLOCK, timeinfo);
            }

            // CRUCIAL: Wait for physical touch release so tap does not trigger twice
            unsigned long rel_start = millis();
            while (digitalRead(XPT_IRQ) == LOW && (millis() - rel_start < 600)) {
                delay(15);
            }
            delay(150); // Debounce
        }
    }

    // 4. Avatar Gaze & Pupil Drift Animation (Active in Page 1 Companion Mode)
    if (current_mode == MODE_COMPANION) {
        static unsigned long last_eye_tick = 0;
        if (now - last_eye_tick >= 60) {
            last_eye_tick = now;

            // Natural Blinking
            if (!is_blinking && (now - last_blink_time > 4200)) {
                is_blinking = true;
                last_blink_time = now;
                draw_eyes_v2(EMO_BLINK, 0.0f, 0.0f);
            } else if (is_blinking && (now - last_blink_time > 130)) {
                is_blinking = false;
                last_blink_time = now;
                current_emotion = EMO_NEUTRAL;
            }

            if (!is_blinking) {
                // Lissajous soft-clamped gaze motion
                float t = now / 1000.0f;
                gaze_dx = 5.0f * sinf(t * 0.70f) * cosf(t * 0.23f);
                gaze_dy = 3.5f * sinf(t * 0.51f + 1.3f);
                gaze_dx = constrain(gaze_dx, -5.0f, 5.0f);
                gaze_dy = constrain(gaze_dy, -3.5f, 3.5f);

                draw_eyes_v2(current_emotion, gaze_dx, gaze_dy);
            }
        }

        // Animated Mouth Equalizer (80 ms tick)
        static unsigned long last_mouth_tick = 0;
        if (now - last_mouth_tick >= 80) {
            last_mouth_tick = now;
            if (ai_is_speaking) {
                mouth_level = (float)random(30, 100) / 100.0f;
                if (!story_is_running) {
                    static int speak_duration = 0;
                    if (++speak_duration > 35) {
                        ai_is_speaking = false;
                        speak_duration = 0;
                        mouth_level = 0.0f;
                    }
                }
            } else {
                mouth_level = 0.0f;
            }
            draw_mouth_equalizer_v2(mouth_level);
        }
    }

    // 4b. AI Story Teller Step Advancement in Companion Mode
    if (current_mode == MODE_COMPANION && story_is_running) {
        if (now - last_story_step_time >= STORY_STEP_DURATION_MS) {
            last_story_step_time = now;
            story_step++;
            if (story_step >= TOTAL_STORY_STEPS - 1) {
                story_step = TOTAL_STORY_STEPS - 1; // Cap at Step 5 ("— END —")
                story_is_running = false;
                ai_is_speaking = false;
                mouth_level = 0.0f;
                draw_mouth_equalizer_v2(0.0f);
                current_emotion = EMO_NEUTRAL;
                draw_eyes_v2(EMO_NEUTRAL, 0.0f, 0.0f);
            } else {
                ai_is_speaking = true;
                if (story_step == 1) current_emotion = EMO_THINKING;
                else if (story_step == 3) current_emotion = EMO_HAPPY;
                else current_emotion = EMO_NEUTRAL;
            }
            draw_text_story_viewer();
        }
    }

    // 5. Differential Clock & Live Telemetry Updates (Every 1 second)
    static unsigned long last_clock_tick = 0;
    if (now - last_clock_tick >= 1000) {
        last_clock_tick = now;
        if (current_mode == MODE_CLOCK) {
            update_main_clock_digits(timeinfo, false);
        } else if (current_mode == MODE_COMPANION) {
            update_companion_clock_banner(timeinfo, false);
            if (weather_data_updated) {
                weather_data_updated = false;
                draw_today_weather_card();
                draw_atmosphere_advisory_card();
                draw_text_story_viewer();
            }
        } else if (current_mode == MODE_NIGHT) {
            update_night_clock_digits(timeinfo, false);
        }
    }

    // 6. Secondary OLED HUD Refresh (250 ms)
    static unsigned long last_oled_tick = 0;
    if (now - last_oled_tick >= 250) {
        last_oled_tick = now;
        update_oled_telemetry_hud();
    }

    delay(20);
}
