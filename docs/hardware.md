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
| Optional: up to 4 tact switches (macro buttons) | Not yet purchased. Wired from the GPIO to GND, no resistor (internal pull-up, 20 ms debounce in firmware). Firmware works with 0 to 4 (configurable). |

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
| Display backlight | LED | 21 | LEDC PWM, active high. Module has its own transistor (Q1). |
| Display MISO | SDO (MISO) | **not connected** | See hardware facts |
| Touch pins | T_CLK, T_CS, T_DIN, T_DO, T_IRQ | **not connected** | No touch controller |
| I2C SDA (sensor) | SDA | 1 | 400 kHz. Module usually has pull-ups. |
| I2C SCL (sensor) | SCL | 2 | |
| Knob port 1 A | CLK | 4 | PCNT unit 0 |
| Knob port 1 B | DT | 5 | |
| Knob port 1 switch | SW | 6 | Internal pull-up (module may not populate one) |
| Knob port 2 A | CLK | 7 | PCNT unit 1 |
| Knob port 2 B | DT | 15 | |
| Knob port 2 switch | SW | 16 | Internal pull-up |
| Knob port 3 A (optional) | CLK | 39 | PCNT unit 2. Former SD card pins; also the pad JTAG pins (JTAG runs over USB here). Not yet verified on the board. |
| Knob port 3 B (optional) | DT | 40 | |
| Knob port 3 switch (optional) | SW | 41 | Internal pull-up |
| Button port 1 (optional) | | 17 | Tact switch to GND, internal pull-up |
| Button port 2 (optional) | | 18 | Tact switch to GND, internal pull-up |
| Button port 3 (optional) | | 8 | Tact switch to GND, internal pull-up |
| Button port 4 (optional) | | 13 | Tact switch to GND, internal pull-up |
| Spare | | 42, 47 | |

Knob and button **numbers** (Knob 1, Button 2, what the config's `enc1`, `btn2` mean) are not tied to ports: `inputs.knob_ports` and `inputs.button_ports` say which port each number reads (Settings > Knobs and buttons > Numbering, where pressing or turning a control assigns it). Default: knob 1 = port 1, knob 2 = port 2, no buttons.

## Power

- Encoders `+`: **3.3 V**.
- Sensor VCC: 3.3 V.
- Display VCC: **5 V** from the board's 5Vin pin. On this board that pin only carries USB power after the `IN-OUT` solder pad is bridged. **Done 2026-11-07: `IN-OUT` bridged, 5Vin reads about 4.8 V on USB, display VCC moved from 3V3 to 5Vin, J1 still open.** The display is clearly brighter (on 3V3 its U1 regulator was starved, about 3.0 V out) and the white screen after a cold plug-in is gone. The module has an onboard 3.3 V regulator (U1) with bypass jumper J1 open. Logic levels are 3.3 V in all cases; the ESP32-S3 drives them directly.
- **With `IN-OUT` bridged, 5Vin is wired to the USB 5 V rail: never connect another 5 V supply to it while USB is plugged in. Never short J1 on the display while VCC is 5 V.**
- Display LED: driven through the module's own transistor (Q1), so GPIO21 PWM connects straight to the LED pin.
- The final device is powered from the **native USB** port.

## Open hardware checks (fill in when confirmed)

| Question | Answer | How to check |
|---|---|---|
| Which USB-C port is native USB? | Identified by USB ID (2026-11-06): native USB enumerates as `303A:1001` (ESP32-S3 USB-Serial-JTAG before TinyUSB takes over); the bridge is a WCH CH343 (`1A86:55D3`). Physical connector labels not recorded yet. | Silkscreen usually says `USB` (native) vs `COM`/`UART` (bridge). The bridge port shows up as a CH340/CP210x COM port in Device Manager. |
| Onboard RGB LED GPIO (38 or 48) | unknown | Board silkscreen or seller page. Or flash a blink on 48, then 38. |
| Display VCC: regulator / J1 jumper? | From a photo of the back (2026-11-06, board marked "SPI 480X320 V1.0"): U1 is a SOT-23 3.3 V regulator on VCC, and J1 next to it appears open (two separate pads, no solder bridge). **Power VCC from 5 V.** Only short J1 if VCC ever has to come from 3.3 V. | Look for a 3-pin regulator (often `662K` or `AMS1117`) and a `J1` pad near the VCC pin. If present and J1 open: 5 V on VCC. If J1 shorted or no regulator: 3.3 V. |
| Backlight LED pin has a transistor? | Yes (2026-11-06 photo): the LED pin goes through R6 to transistor Q1, which switches the backlight. GPIO21 can drive LED directly with PWM, active high (high = on). No external MOSFET needed. | Look for a small SOT-23 transistor near the LED pin. If the LED pin is wired straight to the backlight, it may draw 40 to 80 mA: drive it through an N-MOSFET (e.g. AO3400) or NPN. Measure current at full brightness before connecting to a GPIO. |
| Board 5V pin as an output | **Yes, after bridging `IN-OUT`** (2026-11-07): about 4.8 V on USB, same with one or both cables. As shipped it read 1.6 to 1.7 V (YD-ESP32-S3 style board: 5Vin separated from USB VBUS by a diode unless `IN-OUT` is bridged). Since the bridge, never feed 5 V into that pin from another supply. | Multimeter between 5Vin and GND with the board on USB. |
| Sensor I2C addresses | AHT20 at 0x38, BMP280 at **0x77** (2026-11-07 I2C scan). | `i2c` console command |
| Encoder counts per detent (2 or 4) | **4** for enc1 and on enc2's pins (2026-11-07, same encoder moved to GPIO 7/15/16): 10 slow clicks gave 40 PCNT counts; a fast back-and-forth spin returned to exactly 0 (no lost counts). An earlier static pin reading suggested 2, which was wrong. Wiring confirmed. Check enc2 when fitted. | Phase 2 debug view prints raw PCNT counts; turn one detent and read the delta. |
| Physical buttons | 2 fitted (2026-11-09), up to 4 | Button ports GPIO 17, 18, 8, 13; numbering in `inputs.button_ports`. |
| Enclosure / sensor placement | undecided | Affects self-heating offset and display orientation. |
