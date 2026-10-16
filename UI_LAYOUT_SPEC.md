# CYD AI Companion & Weather Analyzer — UI Specification v2.1
> **Cyclic 3-Page Touch Navigation & Calm Dashboard Architecture**  
> Designed for ESP32-2432S028R ("Cheap Yellow Display" / CYD) with secondary 0.96" OLED.

---

## 1. Target Hardware
| Subsystem | Part | Bus | Key Pins |
| :--- | :--- | :--- | :--- |
| **Primary display** | 2.8" TFT 320×240, ILI9341 | SPI @ 40 MHz | MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, RST −1 |
| **Backlight** | LEDC PWM ch0, 5 kHz, 8-bit | — | GPIO 21 |
| **Touch** | XPT2046 resistive | SPI (software bitbang) | CS 33, IRQ 36, MOSI 32, MISO 39, CLK 25 |
| **Secondary display** | 0.96" OLED 128×64, SSD1306 | I²C @ 400 kHz | SDA 27, SCL 22, addr `0x3C` |
| **Ambient light sensor** | On-board LDR | ADC1 | GPIO 34 |
| **RGB LED** | Common anode (active LOW) | — | R 4, G 16, B 17 |
| **microSD** | Must be de-asserted | SPI (shared) | CS 5 — drive HIGH |

---

## 2. Screen Map & Cyclic Touch Navigation (v2.1)

```
                    ┌─────────────────────────┐
                    │    0  SPLASH (1.5 s)    │
                    └────────────┬────────────┘
                                 │
                ┌────────────────┴────────────────┐
                │        22:00 – 06:30 ?          │
                └────────┬────────────────┬───────┘
                     yes │                │ no
                         ▼                ▼
            ┌─────────────────────┐   ┌────────────────────────────────┐
     ┌─────►│  MODE C: NIGHT      │   │  PAGE 0: MAIN DISPLAY CLOCK    │◄────┐
     │      │  (Warm Amber Dim)   │   │  (Ambient Desk Clock Glance)   │     │
     │      └──────────┬──────────┘   └───────────────┬────────────────┘     │
     │                 │ tap to wake                  │ 1st Tap              │
     │                 └──────────────────────────────┤                      │
     │                                                ▼                      │
     │                                ┌────────────────────────────────┐     │
     │                                │  PAGE 1: CLOCK + TODAY WEATHER │     │
     │                                │          & AI COMPANION        │     │
     │                                │  (Avatar, Mood, AI Advisory)   │     │
     │                                └───────────────┬────────────────┘     │
     │                                                │ 2nd Tap              │
     │                                                ▼                      │
     │                                ┌────────────────────────────────┐     │
     │                                │  PAGE 2: 4-DAY FORECAST        │     │
     │                                │          ANALYZER              │     │
     │                                │  (Deep Synoptic Dashboard)     │     │
     │                                └───────────────┬────────────────┘     │
     │                                                │ 3rd Tap (Loops back) │
     └──────── Bedtime Schedule (if Clock active) ────┴──────────────────────┘
```

### Navigation Rules:
1. **Single Tap Cycle:**
   - On **Page 0 (Main Clock)**: Tap anywhere on screen -> Advances to **Page 1 (Clock with Today Weather & AI Companion)**.
   - On **Page 1 (Companion)**: Tap AI avatar face -> Poke interaction (happy eyes, mouth wave, cyan LED pulse); Tap anywhere else on screen -> Advances to **Page 2 (Forecast Analyzer)**.
   - On **Page 2 (Forecast)**: Tap anywhere on screen -> Returns to **Page 0 (Main Clock)**.
   - On **Mode C (Night Clock)**: Tap anywhere on screen -> Wakes directly to **Page 0 (Main Clock)**.
2. **Sticky Page Persistence (No Auto-Timeout):**
   - The display **NEVER** abruptly reverts to the clock or home screen automatically. Whichever screen you tap to remains active until you tap again.
3. **Physical Release Wait & Debounce:**
   - Tap detection waits for finger release (`digitalRead(XPT_IRQ) == HIGH`) followed by 150ms debounce to prevent runaway page turns.

---

## 3. Color Tokens (RGB565 Verified)
| Token | RGB565 | TrueColor | Usage |
| :--- | :--- | :--- | :--- |
| `C_BG` | `0x0862` | `#0B0F14` | Deep cosmic slate screen background |
| `C_SURFACE` | `0x10C4` | `#141A22` | Card container background fill |
| `C_SURFACE_ALT` | `0x1926` | `#1C2430` | Chips, speech bubbles, eye sockets |
| `C_BORDER` | `0x2188` | `#263241` | Card outlines, dividers, meter tracks |
| `C_TEXT_HI` | `0xF7BF` | `#F2F6FA` | Primary values, clock digits |
| `C_TEXT_MID` | `0x9D36` | `#9AA7B4` | Subtitles, labels, field names |
| `C_TEXT_LOW` | `0x5B4F` | `#5C6B7A` | Page navigation hints, status details |
| `C_ACCENT` | `0x3DFF` | `#38BDF8` | Neon Sky Accent — Hero rails, active dots, `:SS` |
| `C_OK` | `0x3693` | `#34D399` | Online dot, healthy status |
| `C_WARN` | `0xFDE4` | `#FBBF24` | Sun rays, alerts, advisory text |
| `C_DANGER` | `0xFB8E` | `#F87171` | Storm alerts, calibration crosshairs |
| `C_TEAL` | `0x2EB7` | `#2DD4BF` | Conditions, comfort indexes, sensor chips |
| `C_NIGHT_AMBER` | `0xFDA9` | `#FFB74D` | Soft night clock digits |
| `C_NIGHT_DIM` | `0x3A2A` | `#3A4750` | Night secondary text |

---

## 4. Page 0 — Main Display Clock
- **Top Status Bar (`8, 6, 304, 24, r:12`):**
  - Online Dot at `(22, 18)` radius 4 in `C_OK` (green) or `C_WARN` (yellow).
  - Location: `KOTTE, LK` in Font 1 (`C_TEXT_HI`) at `(34, 14)` `TL_DATUM`.
  - Sensor Chip: `(132, 11, 72, 14, r:7)` with `"AI READY"` in `C_TEAL` at `(168, 14)` `TC_DATUM`.
  - Mode Chip: `"DESK CLOCK"` in `C_ACCENT` at `(298, 14)` `TR_DATUM`.
- **Hero Main Clock Container Card (`8, 36, 304, 126, r:12`):**
  - Left Accent Rail: `(18, 46, 4, 106, r:2)` in `C_ACCENT`.
  - Big Time Digits: `HH:MM` centered `TC_DATUM` at `(160, 44)` in Font 6 (`C_TEXT_HI`).
  - Seconds Progress Bar: `(80, 98, 160, 4, r:2)` running smoothly in `C_ACCENT`.
  - Day & Full Date: `"MONDAY · 12 OCTOBER 2026"` centered `TC_DATUM` at `(160, 110)` in Font 2 (`C_TEXT_HI`).
  - Timezone Subtitle: `"SRI LANKA STANDARD TIME · UTC+05:30"` at `(160, 134)` in Font 1 (`C_TEXT_MID`).
- **Bottom Weather Glance Card (`8, 168, 304, 46, r:10`):**
  - Left Vector Icon at `(32, 191)` radius 11.
  - Temperature: `28.5` in Font 4 (`C_TEXT_HI`) at `(52, 176)` + `°C` in Font 2 (`C_ACCENT`) at `(102, 180)`.
  - Divider: Vertical line at `x: 128, y: 174..208` in `C_BORDER`.
  - Condition: `weather.condition_text` in Font 1 (`C_TEAL`) at `(138, 176)`.
  - High/Low & Humidity: `"H 31°  L 22° · RH 68%"` in Font 1 (`C_TEXT_MID`) at `(138, 192)`.
  - Right Action Button: `"DETAILS"` pill `(244, 176, 56, 26, r:6)` in `C_ACCENT`.
- **Bottom Navigation Strip (`y: 220..236`):**
  - Page Dots: `[ ●  ○  ○ ]` centered at `x: 150, 160, 170`.
  - Nav Subtitle: `"TAP SCREEN FOR WEATHER & AI COMPANION"` at `(160, 230)` `TC_DATUM` in Font 1 (`C_TEXT_LOW`).

---

## 5. Page 1 — Clock with Today Weather & AI Companion
- **Top Clock Banner (`8, 6, 304, 48, r:10`):**
  - Accent Rail: `(16, 12, 3, 36, r:1)` in `C_ACCENT`.
  - Compact Time: `HH:MM` in Font 4 (`C_TEXT_HI`) at `(26, 14)` + `:SS` in Font 2 (`C_ACCENT`) at `(92, 20)`.
  - Vertical Divider: `x: 132, y: 12..48` in `C_BORDER`.
  - Short Date: `"MON · 12 OCT"` in Font 2 (`C_TEXT_HI`) at `(142, 12)`.
  - Subtitle: `"KOTTE, LK · AI READY"` in Font 1 (`C_TEAL`) at `(142, 32)`.
  - Right Navigation Hint: `"[FORECAST >]"` in Font 1 (`C_ACCENT`) at `(298, 20)` `TR_DATUM`.
- **Left Column: AI Companion & Story Viewer (`8, 60, 148, 154, r:10`):**
  - Header: `"AI COMPANION"` in Font 1 (`C_ACCENT`) at `(18, 68)` + `● ACTIVE` in `C_OK` at `(146, 68)`.
  - Eye Sockets: `(30, 84, 40, 36, r:10)` and `(86, 84, 40, 36, r:10)` in `C_SURFACE_ALT`.
  - Animated Gaze Tracking: Lissajous smooth motion soft-clamped to (±5, ±3.5).
  - Mouth Equalizer: 5 bars with uniform 9px pitch, baseline `y: 134`.
  - **Embedded Text Story Viewer (`12, 140, 140, 70, r:6`):**
    - Directly situated below the avatar mouth (replaces tap-to-talk and hardware specs).
    - Header: `"🎙 AI STORY"` in Font 1 (`C_ACCENT`) at `(16, 144)` with step tag `[1/5]`..`[5/5]` or `[END]` at `(148, 144)`.
    - Divider: `x: 14, y: 153, w: 136` in `C_BORDER`.
    - **Auto-Play Narrative Engine (3.5s per chapter):**
      - **Step 0 — Today's Weather:** Current temperature, condition, daily high/low, and geographic location.
      - **Step 1 — Atmosphere & Comfort:** Feels-like temperature, relative humidity %, barometric pressure hPa, wind speed and gusts.
      - **Step 2 — Weather & UV Advisory:** Live UV index reading, sun exposure warning, precipitation forecast, and recommendations.
      - **Step 3 — 4-Day Forecast of Weather:** Synoptic preview of +1D, +2D, and +3D with max/min temperatures and weather codes.
      - **Step 4 — Synoptic Summary & Outlook:** Weekly weather trend analysis, temperature peaks, and outdoor planning advice.
      - **Step 5 — Briefing Concluded (Explicit "END"):** Concluding screen featuring prominent Font 2 amber **`"— END —"`**, standby confirmation, and tap-to-replay prompt.
    - **Avatar & Speech Synchronization:** While narrative is active (Steps 0–4), avatar mouth equalizer actively animates (`ai_is_speaking = true`), settling to idle when reaching `"— END —"`.
    - **Interactive Controls:** Tapping the left card (avatar face or story viewer) immediately replays the story narrative.
- **Right Column Upper: Today Live Weather (`162, 60, 150, 72, r:10`):**
  - Header: `"TODAY WEATHER"` + Live Dot at `(298, 72)` in `C_OK`.
  - Vector Weather Icon at `(280, 82)` radius 11.
  - Temperature: `28.5` in Font 4 (`C_TEXT_HI`) at `(172, 80)` + `°C` in Font 2 (`C_ACCENT`) at `(224, 84)`.
  - Condition: `weather.condition_text` in Font 1 (`C_TEAL`) at `(172, 108)`.
  - High/Low: `"H 31°   L 22°"` in Font 1 (`C_TEXT_MID`) at `(172, 120)`.
- **Right Column Lower: Atmospheric Telemetry & Advisory Card (`162, 136, 150, 78, r:10`):**
  - Fill: `C_SURFACE_ALT`, Border: `C_BORDER`.
  - Header: `"⚡ ATMOSPHERE"` in Font 1 (`C_ACCENT`) at `(172, 142)` + `"LIVE"` in `C_OK` at `(302, 142)`.
  - Divider: `x: 170, y: 152, w: 134` in `C_BORDER`.
  - Line 1: Feels like temp & UV Index (`Feels 31° · UV 6.2`).
  - Line 2: Relative humidity & barometric pressure (`RH 68% · 1012hPa`).
  - Line 3: Wind speed & gusts (`Wind 12km/h (G 18)`).
  - Line 4: Live action advisory (`Carry a light umbrella` / `Stable conditions`).
  - Line 5: Rain accumulation & air quality (`Rain 0.0mm · Air: OK`).
- **Bottom Navigation Strip (`y: 220..236`):**
  - Page Dots: `[ ○  ●  ○ ]` centered at `x: 150, 160, 170`.
  - Nav Subtitle: `"TAP SCREEN FOR 4-DAY FORECAST"` at `(160, 230)` `TC_DATUM` in Font 1 (`C_TEXT_LOW`).

---

## 6. Page 2 — 4-Day Forecast Analyzer
- **Top Header Bar (`8, 6, 304, 26, r:10`):**
  - Title: `"FORECAST ANALYZER"` in Font 2 (`C_TEXT_HI`) at `(18, 11)`.
  - Subtitle: `"4-DAY SYNOPTIC"` in Font 1 (`C_TEXT_MID`) at `(176, 13)`.
  - Return Button: `"[ < MAIN CLOCK ]"` in Font 1 (`C_WARN`) at `(298, 11)` `TR_DATUM`.
- **Dual Telemetry Cards (`y: 36..120, h: 84`):**
  - Left Hero Card: Current Temp, icon, condition, feels like, today High/Low.
  - Right Atmosphere Card: Humidity (`68 %`), Pressure (`1012 hPa`), Wind (`12 km/h`), Rain (`0.0 mm`).
- **4-Day Forecast Strip (`8, 126, 304, 66, r:8`):**
  - 4 columns (`x: 46, 122, 198, 274`, 76px width each).
  - Labels: `"TODAY"`, `"+1 DAY"`, `"+2 DAYS"`, `"+3 DAYS"`.
  - Parametric vector icons + High/Low temperatures (`31/22°`, `29/21°`, etc.).
- **AI Forecast Advisory Strip (`8, 198, 304, 20, r:8`):**
  - Accent Dot at `(20, 208)` radius 3 in `C_ACCENT`.
  - Text: `"AI: LOW PRECIPITATION · STABLE UV · CARRY SHADE"` in Font 1 (`C_TEXT_HI`) at `(28, 204)`.
- **Bottom Navigation Strip (`y: 224..238`):**
  - Page Dots: `[ ○  ○  ● ]` centered at `x: 150, 160, 170`.
  - Nav Subtitle: `"TAP SCREEN TO RETURN TO MAIN CLOCK"` at `(160, 232)` `TC_DATUM` in Font 1 (`C_TEXT_LOW`).

---

## 7. Mode C — Night Clock
- Engaged automatically between 22:00 and 06:30, or via bedtime schedule.
- Backlight dimmed to `35/255`, background pure `C_BLACK (0x0000)`.
- Large time `HH:MM` at `(160, 52)` `TC_DATUM` in `C_NIGHT_AMBER (0xFDA9)`.
- Seconds bar `(100, 140, 120, 3, r:2)` in `C_NIGHT_DIM`.
- Date centered at `(160, 158)` `TC_DATUM` in `C_NIGHT_DIM`.
- Weather line centered at `(160, 186)` `TC_DATUM` in `C_NIGHT_DIM`.
- Hint pill `(116, 208, 88, 20, r:10)` with `"TAP TO WAKE"` centered at `(160, 214)`.
- Tap anywhere wakes directly to Page 0 Main Display Clock.

---

## 8. Secondary OLED Telemetry HUD (128×64)
- **Header:** Inverse bar `"AI COMPANION"` + rotating activity spinner `[/]`.
- **CPU Sparkline:** Real-time 26-sample history graph (`34%`).
- **HEAP Gauge:** Proportionally filled memory bar (`142K`).
- **NPU Neural Waveform:** Dynamic sine wave reflecting real-time neural throughput (`LIVE` / `RUN`).
- **TOPS Accelerator Telemetry:** Complete replacement of WiFi signal. Displays `TOPS` + live metric (`2.84` / `1.42`) + 6-segment active accelerator bar gauge.
- **Footer:** Displays active page mode (`CLK-AI`, `STRY-AI`, `CMP-AI`, `FC-AI`, `NGT-AI`) and narrative status (`STORY 1/5`..`STORY 5/5`, `END`, or `NOMINAL`).

