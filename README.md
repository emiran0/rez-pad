# rez-pad

A USB desk companion for Windows: an ESP32-S3 with a 3.5" screen and two clickable knobs, connected to the PC by one USB-C cable. It shows what is playing, how the PC is doing, the weather and the room climate, and it works as a media remote and macro pad.

You build the hardware from common modules (no custom PCB, no soldering beyond headers and wires), flash the firmware and install the Windows app. Everything on the screen is configured from a page in your browser.

## Features

- **Now Playing**: title, artist, album art, progress and controls for whatever plays in Windows (Spotify, browsers, any app that shows up in the Windows media overlay). Seek, volume and mute from the knobs. The app you used last stays on screen while paused; switch to another app from a knob, or set preferred apps in the settings.
- **System**: CPU and GPU load, temperatures, clocks and power, RAM, VRAM, disks, network, fans. Values match Task Manager.
- **Weather and room**: current weather and forecast from [Open-Meteo](https://open-meteo.com/) (no account or API key), plus live room temperature, humidity, pressure and pressure trend from the device's own sensor.
- **Volume mixer**: a widget listing the apps in the Windows volume mixer, as rows or vertical faders; focus it with a button, pick an app with one knob and set its volume with the other. The status bar shows what the knobs do while it is focused.
- **Status bar**: clock, page name, page dots and up to 3 small items of your choice (date, room or outdoor temperature, volume, any system value).
- **Spectrum**: bars that move with whatever the PC plays (captured on the PC, sent over USB about 30 times a second).
- **Homelab integrations** (host-side, credentials stay on the PC): Beszel machines as data sources for every system widget and as a server card (CPU, memory, disk, load, temperature, network).
- **Widget grid editor** at `http://127.0.0.1:47800`: up to 8 pages, each with its own grid (up to 8x8) and colors. Widgets: now playing, album art, track info, media controls, progress, volume, volume mixer, spectrum, metric value / bar / gauge / graph, server card, clock, room climate, weather now, forecast, text label, and images (pictures and animated GIFs). The editor shows a live preview of the device screen.
- **Knobs and buttons**: turn, press-and-turn, click, double click and long press, each mapped to an action (pages, media, volume, volume of just the playing app, seek, brightness, screen off), a key shortcut the device types itself, or a host macro (keys, text, waits, opening programs). Up to 3 knobs and 4 extra buttons; choose which physical control is which by turning or pressing it.
- **Knob popup**: when a knob changes the volume, the playing app's volume, the track position or the brightness and the page has no widget for it, a small card in the middle of the screen shows the new value for 3 seconds (can be turned off).
- **Firmware updates from the config page**: the app has the matching firmware built in and writes it over the same USB cable; layout, settings and pictures stay.
- **Works without the app**: the device keeps its layout in flash; without the host it still shows the room climate and the last weather, and the knobs send media keys over USB.
- **Screen power**: night dimming, off while the PC sleeps, wakes on a knob.
- **Other screens and orientations** (not in a release yet): the 3.5" display in portrait or landscape, or a 2.0" ST7789V (240x320) on the same wires, chosen in Settings > Screen; each screen shape has its own default layout.
- No Wi-Fi on the device: nothing to set up, nothing on your network.

## Hardware

### Parts

| Part | Notes |
|---|---|
| ESP32-S3 **N16R8** dev board (DevKitC-1 style, two USB-C ports) | 16 MB flash and 8 MB PSRAM are required. One port is native USB (the device runs from it), the other a USB-UART bridge (for flashing). |
| 3.5" SPI TFT, **ILI9488**, 480x320 | The common red module with an SD slot. Touch is not used. |
| AHT20 + BMP280 combo module | I2C temperature, humidity and pressure. |
| 2x KY-040 rotary encoder module | Clickable knobs. |
| Optional: up to 4 tact switches | Macro buttons, wired from the GPIO to GND. |
| Alternative: 2.0" SPI TFT, **ST7789V**, 240x320 | Instead of the 3.5" display, on the same pins (SCL 12, SDA 11, RST 14, DC 9, CS 10), VCC on 3V3. No backlight pin, so it cannot be dimmed. See [`docs/hardware.md`](docs/hardware.md). |
| Wires, a USB-C data cable | |

### Wiring

| Module pin | ESP32-S3 | Notes |
|---|---|---|
| Display VCC | 5Vin | See "Power" below |
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
| Encoder 1 (pages, seek) CLK / DT / SW | GPIO 4 / 5 / 6 | |
| Encoder 2 (volume, brightness) CLK / DT / SW | GPIO 7 / 15 / 16 | |
| Encoders + / GND | **3V3** / GND | Never 5 V: the module's pull-ups would put 5 V on the ESP32's pins |
| Buttons 1 to 4 (optional) | GPIO 17, 18, 8, 13 | Other side of each switch to GND |

The pin map lives in [`firmware/include/pins.h`](firmware/include/pins.h) and [`docs/hardware.md`](docs/hardware.md).

### Power

- The device is powered from the board's **native USB** port.
- The display runs best from **5 V** (on 3.3 V its onboard regulator starves and the image can stay white after power-up). Many N16R8 boards only connect the 5Vin pin to USB power after the solder pad marked `IN-OUT` is bridged: check with a multimeter (about 4.8 V between 5Vin and GND with USB plugged in). Once bridged, never feed another 5 V supply into that pin.
- Do not short the display's J1 jumper while its VCC is 5 V.
- Keep the sensor away from the board and the display backlight (a short cable or a vented corner); a temperature offset in the settings corrects the rest.

## Install

1. Download `rez-pad-<version>-win-x64.zip` from the [Releases](../../releases) page, unblock it (right-click > Properties > Unblock) and extract it.
2. Follow `README.txt` inside. In short:
   - **Run `Install.cmd`**: installs the app for your user, optionally starts it with Windows, and opens the config page.
   - **Plug the native USB port** straight into the PC (not through a hub). Windows sets it up without drivers.
   - **Install the firmware from the config page**: Device > Firmware > "Install rez-pad" (later: "Update the board"). The app has the matching firmware built in and writes it over the same cable. The image is also in the zip (`firmware\rez-pad-firmware-<version>.bin`, address `0x0`) for esptool or https://espressif.github.io/esptool-js/ through the UART port.

Requirements: Windows 10 or 11, 64-bit. CPU temperature and motherboard fans need the [PawnIO](https://pawnio.eu/) driver and "Start with Windows" (which runs the app with administrator rights); everything else works without them. The app is not code-signed yet, so SmartScreen asks once ("More info" > "Run anyway").

The host has been tested on one PC so far (Ryzen 5 5600X, RTX 3070 Ti, ASUS B450 board, Windows 11). Other CPUs, GPUs and motherboards should work but are untested.

## Using it

- **Tray icon**: blue screen when the device is connected, grey when not. Double-click for the config page; right-click for status, the log and "Start with Windows".
- **Config page** (`http://127.0.0.1:47800`): Layout to arrange pages and widgets (click an empty cell or a gallery card to add, drag to move, drag the corner to resize); Settings for theme, screen, status bar, knobs and buttons, macros, weather and units, room sensor, media apps and integrations; Device for status, images and firmware updates. "Save to device" (Ctrl+S) stores everything on the device. Ctrl+Z undoes.
- **Default knobs**: left knob turns pages, press-and-turn seeks, click goes to the first page, long press turns the screen off. Right knob changes the volume, press-and-turn the brightness, click plays/pauses, double click skips, long press mutes.

## Building from source

Requirements:

- [PlatformIO Core](https://platformio.org/) (`python -m pip install --user platformio`). Run it from **PowerShell or cmd**, not Git Bash.
- .NET 10 SDK.
- Optional, for the firmware unit tests on the PC: a C++ compiler on PATH, e.g. WinLibs (`winget install BrechtSanders.WinLibs.POSIX.UCRT`, then add its `mingw64\bin` folder under `%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_*` to PATH).

### Firmware

```powershell
cd firmware
pio run                      # build
pio run -t upload            # flash through the USB-UART port
pio device monitor           # debug log on the USB-UART port, 115200 baud
pio test -e native           # unit tests (needs gcc)
```

The debug console on the USB-UART port takes commands such as `help`, `info`, `link`, `config`, `screen`, `perf` (3 s frame rate and SPI load; `perf 1` shows LVGL's overlay), `mixer`, `input enc1 rotate 1` (inject a knob gesture), `sensor`, `enc`, `usb`, `keys ctrl+shift+m`, `view debug` and `bench`. `tools/devconsole.py` sends them from a script.

### Host app

```powershell
cd host
dotnet build
dotnet test --project tests/RezPad.Protocol.Tests
dotnet run --project src/RezPad.Host     # tray app, config page at http://127.0.0.1:47800
```

The web editor (`web/`, plain HTML/CSS/JS modules, no build step) is embedded into the exe at build time. Handy endpoints while developing: `/api/status`, `/api/log`, `/api/media`, `/api/sources`, `/api/hardware`, `/api/weather`, `/api/sensor`. `rez-pad.exe --exit` closes a running instance. Log file: `%LOCALAPPDATA%\rez-pad\host.log`.

### Release package

```powershell
powershell -ExecutionPolicy Bypass -File tools\package.ps1     # -SkipFirmware to skip the PlatformIO build
```

Writes `publish\rez-pad-<version>-win-x64.zip` with the self-contained single-file `rez-pad.exe`, the installer (`setup/`), the firmware image and the license files. The version comes from [`VERSION`](VERSION). Tag the release commit (`vX.Y.Z`) before packaging: only a build at its tag with no local changes shows the plain version (`v0.5.0`); other builds add the commit.

### Generated files

- `python tools/gen_protocol_vectors.py`: protocol test vectors shared by the C# and C++ tests.
- `python tools/gen_config_headers.py`: firmware headers from `docs/default-config.json` and `docs/config-test-vectors.json`.
- `python tools/gen_fonts.py`: LVGL fonts (needs Node.js).
- `python tools/gen_weather_icons.py --web web/img/weather` and `python tools/gen_app_icon.py`: icons (need Pillow).

## Versions

One version for the host, the firmware and the installer, in [`VERSION`](VERSION); see [`docs/versioning.md`](docs/versioning.md) and [`CHANGELOG.md`](CHANGELOG.md).

## License

MIT, see [`LICENSE`](LICENSE). Third-party components and their licenses are listed in [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).
