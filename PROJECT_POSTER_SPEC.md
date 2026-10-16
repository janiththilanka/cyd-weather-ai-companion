# CYD AI Weather Companion & Atmospheric Analyzer
## Comprehensive Engineering & Exhibition Poster Dossier

> **Project Identity:** Dual-Screen Intelligent Edge IoT Companion with In-Flash Parametric Vector Graphics, Multi-Stage Narrative Weather Teller & Real-Time Neural HUD.  
> **Target Hardware:** ESP32-2432S028R ("Cheap Yellow Display" / CYD) + 0.96" SSD1306 I²C OLED.  
> **Firmware Language:** C++ / Arduino ESP32 Core v3.x.  

---

# SECTION 1: Poster Title & Header Block

* **Main Title:** CYD AI WEATHER COMPANION & ATMOSPHERIC ANALYZER
* **Subtitle:** An Intelligent Dual-Screen Edge IoT Station with Expressive Facial Dynamics, Multi-Stage Narrative Telemetry & Real-Time Neural Accelerator HUD
* **Key Badges / Tags:** 
  * `Dual-Display Edge Architecture`
  * `Zero-SD Parametric Vector Graphics`
  * `Differential Flicker-Free Engine`
  * `Open-Meteo Cloud REST Synoptic Sync`
  * `6-Stage Weather Story Narrator`

---

# SECTION 2: Project Abstract & Problem Statement

### The Problem
Traditional consumer weather clocks and IoT desk gadgets are static, uninspiring, and cluttered:
1. They display dense grids of raw numbers without contextual explanation or human-friendly interpretation.
2. They rely on heavy SD card bitmaps that cause slow read latencies, bus contention, and screen flickering.
3. System telemetry (CPU, RAM, Accelerator throughput) is either hidden or clutters the primary clock face.
4. They lack interactive companionship and ambient adaptation for nighttime environments.

### The Innovation
The **CYD AI Weather Companion** solves these limitations through an intelligent dual-screen cybernetic architecture:
* **Primary Screen (2.8" Color TFT):** Functions as a human-centric ambient interface featuring an expressive animated AI agent, live synoptic weather forecasting, and an automatic multi-stage text story teller that narratively walks the user through today's atmospheric conditions, UV alerts, and upcoming 4-day trends.
* **Secondary Screen (0.96" OLED):** Operates as a dedicated hardware & neural engine HUD, showing real-time CPU sparkline history, memory consumption, simulated 2.84 TOPS accelerator load, and active narrative status without polluting the main visual experience.
* **In-Flash Mathematical Rendering:** Eliminates all external image files by rendering high-contrast weather icons, glowing eye sockets, and parametric gauges directly through trigonometric and geometric raster primitives.

---

# SECTION 3: Hardware Topology & Pinout Map

```
                          +-------------------------------+
                          |    ESP32-D0WD-V3 (240 MHz)    |
                          |   Dual Core Xtensa LX6, 520K  |
                          +---------------+---------------+
                                          |
          +-------------------------------+-------------------------------+
          | SPI (VSPI @ 40 MHz)           | I2C (Wire @ 400 kHz)          | ADC / GPIO / PWM
          |                               |                               |
          v                               v                               v
+-------------------+           +-------------------+           +-------------------+
|  2.8" Color TFT   |           |  0.96" Mono OLED  |           | Onboard I/O & Sens|
|  320x240 ILI9341  |           |  128x64 SSD1306   |           |                   |
|                   |           |                   |           | - LDR Light Sensor|
| MOSI: GPIO 13     |           | SDA:  GPIO 27     |           |   (ADC1 GPIO 34)  |
| MISO: GPIO 12     |           | SCL:  GPIO 22     |           | - Backlight PWM   |
| SCLK: GPIO 14     |           | Addr: 0x3C / 0x3D |           |   (LEDC GPIO 21)  |
| CS:   GPIO 15     |           +-------------------+           | - RGB LED (Active |
| DC:   GPIO 2      |                                           |   LOW): 4, 16, 17 |
| RST:  -1 (EN)     |           +-------------------+           | - SD CS: GPIO 5   |
+-------------------+           | XPT2046 Touch     |           |   (Driven HIGH)   |
                                | Software SPI      |           +-------------------+
                                | CLK:25, CS:33     |
                                | MOSI:32, MISO:39  |
                                | IRQ:36            |
                                +-------------------+
```

### Subsystem Hardware Table
| Subsystem | Component | Bus & Clock | Key GPIOs | Functional Purpose |
| :--- | :--- | :--- | :--- | :--- |
| **Primary Display** | 2.8" TFT (ILI9341 / ST7789) | Hardware SPI @ 40 MHz | MOSI 13, MISO 12, SCLK 14, CS 15, DC 2 | 320×240 high-contrast visual canvas for clock, avatar, story narrative, and 4-day forecast |
| **Secondary Display**| 0.96" OLED (SSD1306) | I²C @ 400 kHz | SDA 27, SCL 22 (Addr `0x3C`) | Dedicated AI Task Manager: CPU sparkline, memory progress bar, NPU wave, and TOPS throughput |
| **Touch Controller** | XPT2046 Resistive Digitizer | Software SPI (Independent) | CS 33, IRQ 36, CLK 25, MOSI 32, MISO 39 | 3-point calibration mapped to screen space with non-volatile NVS flash storage |
| **Ambient Light** | Onboard Photoresistor (LDR) | Analog ADC1 (No WiFi conflict)| GPIO 34 (ADC1_CH6) | Senses room illuminance for intelligent night clock transitions |
| **Backlight Dimmer**| High-Efficiency LED Driver | LEDC Timer 0, 5 kHz, 8-bit | GPIO 21 | Smooth PWM brightness control (255 daylight -> 35 warm night) |
| **Status Beacon** | Tri-Color RGB LED | Direct GPIO (Active LOW) | Red 4, Green 16, Blue 17 | Hardware health status and touch event pulse feedback |
| **SPI Protection** | MicroSD Card Interface | Shared VSPI Bus | CS 5 | Driven HIGH permanently during setup to avoid bus lockups |

---

# SECTION 4: Dual-Display Cybernetic Architecture

### 1. Primary Screen: 3-Page Cyclic Navigation Flow
```
        [ Touch Screen Tap ]
  +--------------------------------+
  |                                |
  v                                |
+----------------------------+     |
| PAGE 0: MAIN DISPLAY CLOCK |     |
| - 54px High-Contrast Time  |     |
| - Gregorian Date Banner    |     |
| - Instant Weather Glance   |     |
+--------------+-------------+     |
               | Tap Screen        |
               v                   |
+----------------------------+     |
| PAGE 1: AI COMPANION &     |     |
|         WEATHER STORY      |     |
| - Animated Lissajous Eyes  |     |
| - Active Mouth Equalizer   |     |
| - 6-Stage Weather Narrator |     |
| - Atmospheric Telemetry    |     |
+--------------+-------------+     |
               | Tap Screen        |
               v                   |
+----------------------------+     |
| PAGE 2: 4-DAY FORECAST     |     |
|         SYNOPTIC ANALYZER  |     |
| - Dual Telemetry Cards     |     |
| - 4-Column Forecast Strip  |     |
| - High/Low & Vector Icons  |     |
+--------------+-------------+     |
               | Tap Screen        |
               +-------------------+
```

* **Bedtime Override (Mode C — Night Clock):** Automatically activates between 22:00 and 06:30 (or via ambient light). Dimmed to 35/255 brightness with a non-disruptive golden amber palette (`#FFB74D`) on pure black. Any tap immediately wakes the device to Page 0.

### 2. Secondary Screen: Real-Time Neural & System Telemetry HUD
The OLED runs a persistent, high-speed telemetry engine updated every 250 ms:
1. **Header:** Inverse high-contrast badge `"AI COMPANION"` + 4-frame rotating execution spinner `[/]`.
2. **CPU Telemetry:** Live percentage readout + 26-sample rolling sparkline history graph.
3. **Memory Telemetry:** Active heap allocation readout (`K`) + proportional hardware progress bar.
4. **NPU Neural Waveform:** Dynamic sine wave reflecting inference throughput (`RUN` when narrating, `LIVE` on idle).
5. **TOPS Accelerator Gauge:** Replaces legacy WiFi signal with a clean `2.84 TOPS` metric and a 6-segment active accelerator block gauge that pulses dynamically during AI narrative speech.
6. **Footer:** Displays active system mode (`CLK-AI`, `STRY-AI`, `CMP-AI`, `FC-AI`, `NGT-AI`) and live narrative stage (`STORY 1/5`..`STORY 5/5`, `END`, or `NOMINAL`).

---

# SECTION 5: The AI Story Teller & Narrative Engine

### Narrative Pipeline
When the user switches to **Page 1 (Companion Page)**, the integrated story teller automatically starts narrating through an autonomous 6-stage weather journey (advancing every 3.5 seconds):

```
+---------------------------------------------------------------------------------+
|  STAGE 0: TODAY'S OVERVIEW                                                      |
|  "28.5°C · Partly cloudy | High 31° · Low 22° | Kotte, Sri Lanka"               |
+---------------------------------------------------------------------------------+
                                      | (3.5s)
                                      v
+---------------------------------------------------------------------------------+
|  STAGE 1: ATMOSPHERE & COMFORT                                                  |
|  "Feels like 31.0°C | RH 68% · 1012hPa | Wind 12km/h (Gust 18) | Stable"         |
+---------------------------------------------------------------------------------+
                                      | (3.5s)
                                      v
+---------------------------------------------------------------------------------+
|  STAGE 2: WEATHER & UV ADVISORY                                                 |
|  "UV Index: 6.2 (High) | Midday sun protection advised | Carry light umbrella"  |
+---------------------------------------------------------------------------------+
                                      | (3.5s)
                                      v
+---------------------------------------------------------------------------------+
|  STAGE 3: 4-DAY FORECAST OF WEATHER                                             |
|  "+1D: 29°/21° Overcast | +2D: 27°/19° Showers | +3D: 30°/20° Clear"            |
+---------------------------------------------------------------------------------+
                                      | (3.5s)
                                      v
+---------------------------------------------------------------------------------+
|  STAGE 4: SYNOPTIC SUMMARY & OUTLOOK                                            |
|  "Mid-week rain incoming | Week peak: 31°C | Hydrate & plan outdoor activities" |
+---------------------------------------------------------------------------------+
                                      | (3.5s)
                                      v
+---------------------------------------------------------------------------------+
|  STAGE 5: CONCLUSION (EXPLICIT END)                                             |
|  "BRIEFING COMPLETE:  — END —  | Weather briefing done | AI Agent on standby"   |
+---------------------------------------------------------------------------------+
```

### Avatar Facial Synchronizer
* **Voice-Simulated Mouth Equalizer:** While narrating (Stages 0–4), the 5-bar mouth equalizer actively dances to simulated acoustic syllables (`ai_is_speaking = true`).
* **Expressive Eye Dynamics:** Uses Lissajous harmonic curves for realistic gaze drift soft-clamped to (±5.0, ±3.5), with pseudo-random blinking every ~4.2 seconds and situational mood states (`EMO_NEUTRAL`, `EMO_HAPPY`, `EMO_THINKING`, `EMO_SLEEP`).
* **Automatic Standby at END:** As soon as Stage 5 (`— END —`) is displayed, the mouth settles to a resting flat line and the avatar enters calm standby.
* **Instant Replay:** Tapping the avatar face card or story viewer immediately restarts the story narrative from Stage 0.

---

# SECTION 6: Key Software Engineering Innovations

### 1. In-Flash Parametric Vector Drawing (Zero-Asset Architecture)
Unlike traditional Arduino weather monitors that store dozens of BMP or PNG files on a FAT32 microSD card (risking bus contention, SD card wear, and file corruption), this firmware generates **100% of all visual elements mathematically at runtime**:
* **Sun Icons:** Center disc with radial trigonometrically calculated light rays `(cos(rad) * r, sin(rad) * r)`.
* **Clouds:** Anti-aliased 3-circle overlapping cloud volumes with flat base bounding boxes.
* **Rain & Showers:** Angle-offset directional rain streaks calculated with parameterized precipitation density.
* **Thunderstorms:** Jagged multi-segment vector lightning bolts with warning amber highlights.

### 2. Differential Partial Redraw Engine (Zero Screen Flicker)
Traditional TFT displays suffer from jarring visual flicker because the CPU clears the entire screen (`fillScreen(BLACK)`) before every frame. This project implements a **differential redraw architecture**:
* Static container borders, card backgrounds, and icons are rendered only once during screen transition.
* Dynamic clock digits are overdrawn using explicit background-colored fonts (`tft.setTextColor(C_TEXT_HI, C_SURFACE)`), changing only the altered pixels.
* The Story Teller refreshes only its dedicated 140×70 pixel container bounding box on stage transitions.

### 3. Open-Meteo REST API & Non-Blocking Sync
* Fetches live synoptic atmospheric telemetry via HTTP GET from Open-Meteo's Colombo/Kotte coordinates (`6.9016° N, 79.9077° E`).
* Parses 16+ atmospheric data points using `ArduinoJson v7` with a zero-copy static document buffer:
  * Temperature, feels-like, relative humidity, pressure, wind speed, wind gusts, UV index, and weather WMO code.
  * 4-day daily maximum, minimum, and forecast codes.

---

# SECTION 7: System Performance & Resource Metrics

| Metric | Measured Value | Constraint / Limit | Efficiency Ratio |
| :--- | :--- | :--- | :--- |
| **Flash Program Storage** | **1,151,882 Bytes** | 1,310,720 Bytes | **87.8%** (Optimal utilization) |
| **Static RAM Allocation** | **51,576 Bytes** | 327,680 Bytes | **15.7%** (Low memory pressure) |
| **Free Heap Headroom** | **~276 KB Free** | — | Highly resilient against OOM crashes |
| **Primary SPI Bus Clock** | **40 MHz** | Max ILI9341 spec | Ultra-fast hardware DMA/SPI transfers |
| **Secondary I²C Clock** | **400 kHz** | SSD1306 Fast-Mode | Crisp 250ms OLED telemetry refreshes |
| **Serial Flashing Speed** | **460,800 Baud** | 115,200 Baud default | **4x faster** firmware upload cycle |
| **Full Screen Refresh Time** | **< 32 ms** | 100 ms threshold | High-framerate responsive touch |

---

# SECTION 8: Suggested Poster Visual Layout (Nano Banana / Canva Guide)

Use this grid structure when assembling your project poster:

```
+---------------------------------------------------------------------------------------------------+
| [LOGO]           CYD AI WEATHER COMPANION & ATMOSPHERIC ANALYZER                   [QR CODE / GITHUB] |
|                  Dual-Screen Edge IoT Station with Expressive Facial Dynamics & Neural Telemetry    |
+---------------------------------+---------------------------------+-------------------------------+
|  1. PROBLEM & OBJECTIVE         |  3. SYSTEM ARCHITECTURE         |  5. AI STORY TELLER ENGINE    |
|  - Limitations of static clocks |  - Block diagram: ESP32 + Dual  |  - 6-Stage Narrative machine  |
|  - SD card dependency issues   |    Displays + Touch + Sensors   |  - Synchronized mouth wave    |
|  - The cybernetic solution      |  - Parametric vector graphics   |  - Explicit "— END —" state   |
|                                 |  - Flicker-free partial redraw  |  - Auto-start on Page 1       |
+---------------------------------+---------------------------------+-------------------------------+
|  2. HARDWARE TOPOLOGY           |  4. UI SCREEN MAP & FLOW        |  6. RESULTS & BENCHMARKS      |
|  - 2.8" TFT (SPI @ 40 MHz)      |  [Page 0] Ambient Hero Clock    |  - Flash: 87%, RAM: 15%       |
|  - 0.96" OLED (I2C @ 400 kHz)   |  [Page 1] Companion & Story     |  - 276 KB Free Heap           |
|  - XPT2046 Touch + NVS Calib    |  [Page 2] 4-Day Forecast Strip  |  - Zero SD card dependency    |
|  - Active-Low RGB + LDR Sensor  |  [Mode C] Bedtime Night Clock   |  - Zero-flicker performance   |
+---------------------------------+---------------------------------+-------------------------------+
|  TEAM: [Your Name / Team ID]    |  LAB: IoT & Embedded AI Lab     |  DATE: October 2026           |
+---------------------------------------------------------------------------------------------------+
```

### Color Palette for Your Poster (Exact RGB565 / HEX Match)
* **Background Canvas:** `#0B0F14` (Deep Cosmic Slate — Sleek dark mode)
* **Card Panels:** `#141A22` (Dark Slate Container) & `#1C2430` (Surface Alt)
* **Neon Accent:** `#38BDF8` (Sky Cyan — Highlights, Title, Eye Iris)
* **Status Green:** `#34D399` (Emerald — Online indicators, Success badges)
* **Alert Amber:** `#FBBF24` (Sun Gold — High temp, Warning alerts)
* **Night Gold:** `#FFB74D` (Soft Warm Amber — Bedtime night clock)
* **Primary Text:** `#F2F6FA` (Crisp White — Primary headings & statistics)
