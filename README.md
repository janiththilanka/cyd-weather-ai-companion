# CYD AI Weather Companion & Atmospheric Analyzer

An intelligent, dual-display IoT desk companion powered by the **ESP32-2432S028R** ("Cheap Yellow Display" / CYD) and a secondary **0.96" SSD1306 OLED**. Features an expressive animated AI agent with Lissajous gaze tracking, audio-simulated mouth equalizer, autonomous 6-stage weather story narrator, 4-day synoptic atmospheric telemetry, and a persistent hardware neural accelerator HUD.

---

## Key Highlights

- **Dual-Display Architecture:**
  - **Primary Screen (2.8" Color TFT, 320x240):** Ambient hero clock, live weather telemetry, animated AI companion, and 4-day synoptic forecast.
  - **Secondary Screen (0.96" OLED, 128x64):** Dedicated AI hardware telemetry HUD featuring live CPU sparkline history, RAM allocation, neural activity waveform, and 2.84 TOPS accelerator load meter.
- **In-Flash Parametric Vector Graphics:**
  - Zero SD card dependency.
  - Sun, cloud, rain, and thunderstorm icons rendered dynamically at runtime via mathematical vector primitives.
- **Differential Flicker-Free Engine:**
  - Overdraws only dynamic clock digits and step narrative boxes without clearing the screen.
- **Autonomous Multi-Stage Story Narrator:**
  - Automatically walks through Today's Weather, Comfort Index, UV / Action Advisory, 4-Day Forecast, and Weekly Outlook, concluding with an explicit `"— END —"`.
- **Zero-Disturbance Bedtime Mode:**
  - Automatically transitions between 22:00 and 06:30 into a warm amber night clock dimmed to 35/255 brightness.

---

## Hardware Pinout Map

| Subsystem | Part | Bus | Pins | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Primary Display** | 2.8" TFT (ILI9341/ST7789) | Hardware SPI (VSPI @ 40 MHz) | MOSI: `13`, MISO: `12`, SCLK: `14`, CS: `15`, DC: `2` | RST tied to EN |
| **Backlight** | Onboard LED Driver | LEDC PWM (5 kHz, 8-bit) | Backlight: `GPIO 21` | Brightness `35..255` |
| **Secondary Display**| 0.96" OLED (SSD1306) | I²C (Wire @ 400 kHz) | SDA: `27`, SCL: `22` | I2C Addr: `0x3C` |
| **Touchscreen** | XPT2046 Resistive | Software SPI | CS: `33`, IRQ: `36`, CLK: `25`, MOSI: `32`, MISO: `39` | 3-point NVS calibrated |
| **Light Sensor** | Onboard LDR | Analog ADC1 | Pin: `GPIO 34` | ADC1_CH6 (No WiFi conflict) |
| **RGB LED** | Common Anode (Active LOW) | GPIO | Red: `4`, Green: `16`, Blue: `17` | Status beacons |
| **MicroSD** | Onboard SD Slot | VSPI (Shared) | CS: `GPIO 5` | Driven HIGH during setup |

---

## Screen Navigation

Touch the screen at any time to cycle across the 3 primary display pages:

```
[Page 0] Main Hero Clock ──(Tap)──> [Page 1] Companion & Story ──(Tap)──> [Page 2] 4-Day Forecast ──(Tap)──> [Page 0]
```

* **Tap Avatar Face or Story Viewer on Page 1:** Restarts the 6-stage weather story narrative from Stage 0.
* **Night Mode (Mode C):** Tap anywhere on screen to wake to Page 0 Main Clock.

---

## Software Dependencies & Libraries

Install these libraries via the **Arduino Library Manager**:

1. **`TFT_eSPI`** by Bodmer
2. **`Adafruit SSD1306`** by Adafruit
3. **`Adafruit GFX Library`** by Adafruit
4. **`ArduinoJson`** by Benoît Blanchon (Version 7.x recommended)

### `TFT_eSPI` Setup for CYD (ESP32-2432S028R)
In your Arduino library directory (`Arduino/libraries/TFT_eSPI/User_Setup.h`), configure:
```cpp
#define ILI9341_2_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define SPI_FREQUENCY  40000000
```

---

## Configuration

In `cyd_weather_ai_companion.ino`:
```cpp
// 1. WiFi Credentials
const char* WIFI_SSID       = "YOUR_WIFI_SSID";
const char* WIFI_PASS       = "YOUR_WIFI_PASSWORD";

// 2. Weather Location (Open-Meteo)
const char* LOCATION_NAME   = "Kotte, LK";
const float LOC_LAT         = 6.9016f;
const float LOC_LON         = 79.9077f;
const char* LOC_TIMEZONE    = "Asia%2FColombo";
```

---

## Documentation Files
- [`UI_LAYOUT_SPEC.md`](UI_LAYOUT_SPEC.md): Complete pixel-exact UI specification with design tokens, coordinates, and color palettes.
- [`PROJECT_POSTER_SPEC.md`](PROJECT_POSTER_SPEC.md): Exhibition & project poster dossier with block diagrams, problem statements, and poster layout blueprints.

---

## License
MIT License. Free for open-source and educational use.
