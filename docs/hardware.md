# Hardware

Pin assignments here are mirrored in `firmware/include/pins.h`. Change both or neither.

## Inventory

| Part | Notes |
|---|---|
| **ESP32-S3 N16R8** dev board, two USB-C ports | 16 MB flash, 8 MB octal PSRAM. One USB-C is native USB (GPIO19/20), the other goes through a USB-UART bridge. **Main MCU.** |
| **3.5" SPI TFT, ILI9488, 320x480**, with SD slot | **Main display.** Used in landscape (480x320). Has touch pins but no touch controller: leave all `T_*` pins unconnected. |
| **AHT20 + BMP280** combo module | I2C. AHT20 = temp/humidity (0x38). BMP280 = pressure/temp (0x76 or 0x77, detected at runtime). |
| **2x rotary encoder modules** (KY-040 style) | Pins: CLK, DT, SW, +, GND. Clickable knob. |
| Raspberry Pi Pico 2 W | Backup MCU. Not used in the main build. |
| 2.0" ST7789V 240x320 TFT | Bench/test display, or a future "mini" variant. Not used in the main build. |
| Raspberry Pi Zero 2 W | Not used. |
| Optional: 1 to 2 tactile buttons | Not yet purchased. Firmware works with zero, one or two buttons (configurable). |

## Hardware facts that drive the design

- **ILI9488 over SPI only accepts 18-bit color (RGB666, 3 bytes/pixel).** A full 480x320 frame is ~460 KB.
  - 40 MHz SPI: ~90 ms per full-screen redraw.
  - 80 MHz SPI: ~45 ms (only if the module is stable at that speed; must be measured).
  - Therefore: no full-screen sliding animations. Page switches are instant redraws. Everything else is partial updates of small regions.
- **ILI9488 modules often do not tri-state MISO (SDO).** Never connect the display's SDO/MISO. If the SD card is used, it goes on a separate SPI bus.
- **KY-040 modules have pull-up resistors to the `+` pin.** Power them from **3.3 V only**, never 5 V.
- **Sensor self-heating:** mount the sensor away from the ESP32-S3 and the display backlight (short cable or vented corner). A user-configurable calibration offset exists in config.

## Pin map (ESP32-S3 N16R8)

> **Status: proposed. Verify against the actual board silkscreen and module labels before wiring.**

### Pins that must NOT be used

- GPIO26 to GPIO37: flash and octal PSRAM.
- GPIO19, GPIO20: native USB D-/D+.
- GPIO43, GPIO44: UART0 to the USB-UART bridge (debug logs).
- GPIO0, GPIO3, GPIO45, GPIO46: strapping pins.
- GPIO38 and GPIO48: onboard RGB LED on most DevKitC-1 style boards (38 on v1.1, 48 on v1.0 and most clones). Both reserved until confirmed.

### Assignment

| Function | Module pin | ESP32-S3 GPIO | Notes |
|---|---|---|---|
| Display SPI SCK | SCK | 12 | SPI2 (FSPI) IOMUX pin, allows high clock |
| Display SPI MOSI | SDI (MOSI) | 11 | SPI2 IOMUX pin |
| Display CS | CS | 10 | SPI2 IOMUX pin |
| Display DC | DC / RS | 9 | |
| Display RESET | RESET | 14 | |
| Display backlight | LED | 21 | LEDC PWM. Check if the module has a backlight transistor; if not, add an N-MOSFET or NPN driver. |
| Display MISO | SDO (MISO) | **not connected** | See hardware facts |
| Touch pins | T_CLK, T_CS, T_DIN, T_DO, T_IRQ | **not connected** | No touch controller |
| SD card SCK (optional, later) | SD_SCK | 39 | SPI3 (separate bus) |
| SD card MOSI (optional, later) | SD_MOSI | 40 | |
| SD card MISO (optional, later) | SD_MISO | 41 | |
| SD card CS (optional, later) | SD_CS | 42 | |
| I2C SDA (sensor) | SDA | 1 | 400 kHz. Module usually has pull-ups. |
| I2C SCL (sensor) | SCL | 2 | |
| Encoder 1 A | CLK | 4 | PCNT unit 0 |
| Encoder 1 B | DT | 5 | |
| Encoder 1 switch | SW | 6 | Internal pull-up (module may not populate one) |
| Encoder 2 A | CLK | 7 | PCNT unit 1 |
| Encoder 2 B | DT | 15 | |
| Encoder 2 switch | SW | 16 | Internal pull-up |
| Button 1 (optional) | | 17 | Active low, internal pull-up |
| Button 2 (optional) | | 18 | Active low, internal pull-up |
| Spare | | 8, 13, 47 | |

## Power

- Encoders `+`: **3.3 V**.
- Sensor VCC: 3.3 V.
- Display VCC: many 3.5" ILI9488 modules have an onboard 3.3 V regulator and accept 5 V on VCC (some have a `J1` jumper to bypass it for 3.3 V). **Check the module (see checklist below).** Logic levels are 3.3 V in all cases.
- The final device is powered from the **native USB** port.

## Open hardware checks (fill in when confirmed)

| Question | Answer | How to check |
|---|---|---|
| Which USB-C port is native USB? | unknown | Silkscreen usually says `USB` (native) vs `COM`/`UART` (bridge). The bridge port shows up as a CH340/CP210x COM port in Device Manager. |
| Onboard RGB LED GPIO (38 or 48) | unknown | Board silkscreen or seller page. Or flash a blink on 48, then 38. |
| Display VCC: regulator / J1 jumper? | unknown | Look for a 3-pin regulator (often `662K` or `AMS1117`) and a `J1` pad near the VCC pin. If present and J1 open: 5 V on VCC. If J1 shorted or no regulator: 3.3 V. |
| Backlight LED pin has a transistor? | unknown | Look for a small SOT-23 transistor near the LED pin. If the LED pin is wired straight to the backlight, it may draw 40 to 80 mA: drive it through an N-MOSFET (e.g. AO3400) or NPN. Measure current at full brightness before connecting to a GPIO. |
| Encoder counts per detent (2 or 4) | unknown | Phase 2 debug view prints raw PCNT counts; turn one detent and read the delta. |
| Physical buttons | undecided (0, 1 or 2) | Configurable in `inputs`. |
| Enclosure / sensor placement | undecided | Affects self-heating offset and display orientation. |
