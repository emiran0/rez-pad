# rez-pad hardware

What to buy, how to wire it and how to power it. Everything is common modules on a breadboard or with jumper wires: no custom PCB, no soldering beyond headers. Engineering notes (pin choices, measurements, open checks) are in [`docs/hardware.md`](docs/hardware.md).

## Parts

| Part | Notes |
|---|---|
| ESP32-S3 **N16R8** dev board (DevKitC-1 style, two USB-C ports) | 16 MB flash and 8 MB PSRAM are required. One port is native USB (the device runs from it), the other a USB-UART bridge (for flashing and the debug console). |
| 3.5" SPI TFT, **ILI9488**, 480x320 | The common red module with an SD slot. Touch is not used. |
| AHT20 + BMP280 combo module | I2C temperature, humidity and pressure. |
| 2x KY-040 rotary encoder module | Clickable knobs. A third one is supported (see "Wiring"). |
| Optional: up to 4 tact switches | Macro buttons, wired from the GPIO to GND. |
| Alternative: 2.0" SPI TFT, **ST7789V**, 240x320 | Instead of the 3.5" display, on the same pins. No backlight pin, so it cannot be dimmed. See "Using the 2.0-inch display". |
| Wires, a USB-C data cable | |

## Wiring

### 3.5" display, sensor, knobs and buttons

| Module pin | ESP32-S3 | Notes |
|---|---|---|
| Display VCC | 5Vin | See "Power" |
| Display GND | GND | |
| Display CS | GPIO 10 | |
| Display RESET | GPIO 14 | |
| Display DC / RS | GPIO 9 | |
| Display SDI (MOSI) | GPIO 11 | |
| Display SCK | GPIO 12 | |
| Display LED | GPIO 21 | Backlight PWM |
| Display SDO (MISO) | **not connected** | The ILI9488 does not release this line and would disturb the bus |
| Display T_CLK, T_CS, T_DIN, T_DO, T_IRQ | **not connected** | |
| Sensor VCC / GND | 3V3 / GND | |
| Sensor SDA / SCL | GPIO 1 / GPIO 2 | |
| Knob 1 (pages, seek) CLK / DT / SW | GPIO 4 / 5 / 6 | |
| Knob 2 (volume, brightness) CLK / DT / SW | GPIO 7 / 15 / 16 | |
| Knob 3 (optional) CLK / DT / SW | GPIO 39 / 40 / 41 | Not yet tested on a board |
| Knobs + / GND | **3V3** / GND | Never 5 V: the module's pull-ups would put 5 V on the ESP32's pins |
| Buttons 1 to 4 (optional) | GPIO 17, 18, 8, 13 | Other side of each switch to GND |

Which physical knob or button is "Knob 1", "Button 2" and so on can be changed later in the config page (Settings > Knobs and buttons > Assign), so swapped wires are not a problem.

### Using the 2.0-inch display

The 2.0" ST7789V module (pins GND, VCC, SCL, SDA, RST, DC, CS) goes on the same wires as the 3.5" display; connect only one display at a time.

| Module pin | ESP32-S3 | Notes |
|---|---|---|
| GND | GND | |
| VCC | **3V3** | Some versions of this module take 5 V, others do not; 3V3 works for all of them (one tested module froze on 5 V) |
| SCL | GPIO 12 | Same wire as the 3.5" display's SCK |
| SDA | GPIO 11 | Same wire as SDI (MOSI) |
| RST | GPIO 14 | |
| DC | GPIO 9 | |
| CS | GPIO 10 | |

Then choose it in the config page: Settings > Screen > Display. Until then the board drives it as a 3.5" display and it stays blank or garbled. Its light is always on (no backlight pin), so brightness, night dimming and screen off do not change it.

## Power

- The device is powered from the board's **native USB** port.
- The 3.5" display runs best from **5 V** (on 3.3 V its onboard regulator starves and the image can stay white after power-up). Many N16R8 boards only connect the 5Vin pin to USB power after the solder pad marked `IN-OUT` is bridged: check with a multimeter (about 4.8 V between 5Vin and GND with USB plugged in). Once bridged, never feed another 5 V supply into that pin.
- Do not short the 3.5" display's J1 jumper while its VCC is 5 V.
- The 2.0" display goes on **3V3** (see above).
- Plug the native USB port straight into the PC, not through a USB hub.

## Tips

- **Unplug USB before moving a display or any wire.** The display is set up once when the board starts; one that loses contact while the board runs stays blank, frozen or garbled until the board restarts.
- **A frozen image while everything else works** (knobs still control the PC) usually means a loose display wire: CS, SDA/SDI or SCL/SCK. Unsoldered header pins on the display module are the usual cause.
- **Keep the sensor away from the board and the display backlight** (a short cable or a vented corner). A temperature offset in the settings corrects the rest.

The pin map lives in [`firmware/include/pins.h`](firmware/include/pins.h), [`docs/hardware.md`](docs/hardware.md) and this file; a pin change updates all three.
