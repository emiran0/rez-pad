# rez-pad

USB desk companion: ESP32-S3 + 3.5" ILI9488 display, driven by a Windows tray app. See `CLAUDE.md` for the overview and `docs/` for specs.

## Requirements

- PlatformIO Core (`python -m pip install --user platformio`). Run it from **PowerShell or cmd**, not Git Bash.
- .NET 10 SDK.
- Optional, for firmware unit tests on the PC: a host C++ compiler on PATH. Tested with WinLibs (`winget install BrechtSanders.WinLibs.POSIX.UCRT`). Winget does not create `gcc`/`g++` aliases for it, so add its `mingw64\bin` folder (under `%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_*`) to your user PATH and open a new terminal.

## Firmware

```powershell
cd firmware
pio run                      # build
pio run -t upload            # flash through the USB-UART bridge port
pio device monitor           # debug log on UART0, 115200 baud
pio test -e native           # protocol unit tests (needs gcc)
```

## Host

```powershell
cd host
dotnet build
dotnet test --project tests/RezPad.Protocol.Tests
dotnet run --project src/RezPad.Host   # tray app, config page at http://127.0.0.1:47800
```

The tray app finds the board on its native USB port by itself (VID 303A, PID 4005, serial `RP-...`). Useful bits:

- Tray menu: connection status, open config page, open log file.
- `rez-pad.exe --exit` closes the running instance cleanly.
- `GET http://127.0.0.1:47800/api/status`, `/api/log`, `/api/media` and `/api/sources` for status, recent log lines, the current Now Playing / volume, and the system data sources with live values.
- Weather: pick a city on the config page (Open-Meteo, no API key); units there too. `GET /api/weather`, `/api/sensor` (the device's room sensor).
- Config editor: open the config page from the tray (or `http://127.0.0.1:47800`). Layout: pick a page on the left, click a widget in the gallery (or an empty cell first) to add it, drag widgets to move or swap them, drag the corner to resize, set each page's grid, colors and knobs above the screen. Image widgets take uploaded pictures and GIFs. Settings has the theme, screen, knobs and buttons (actions, key shortcuts the device types itself, or macros), macros, weather and sensor options. "Save to device" (Ctrl+S) validates, saves `%APPDATA%\rez-pad\config.json` and shows the device's answer; Ctrl+Z undoes. The device keeps the config and images in flash and boots into them without the host.
- CPU temperature and motherboard fans need the [PawnIO](https://pawnio.eu/) driver and admin rights. Install PawnIO, then use the tray's "Start with Windows (full sensor access)", which registers an elevated logon task (one UAC prompt). GPU sensors work without either.
- Plug the board's native USB port straight into the PC, not through a USB 2.0 hub shared with the UART cable (see `docs/measurements.md`).
- Log file: `%LOCALAPPDATA%\rez-pad\host.log`.

## Release package

```powershell
powershell -ExecutionPolicy Bypass -File tools\package.ps1     # -SkipFirmware to skip the PlatformIO build
```

Writes `publish\rez-pad-<version>-win-x64.zip`: a self-contained single-file `rez-pad.exe` (web editor embedded, no .NET install needed), `Install.cmd` / `Uninstall.cmd` (`setup/install.ps1`: per-user install to `%LOCALAPPDATA%\Programs\rez-pad`, Start menu and Settings > Apps entries, optional "Start with Windows" task, PawnIO hint) and the firmware image (`firmware.factory.bin`, flash at 0x0). The version comes from `host/Directory.Build.props`. App and tray icons: `python tools/gen_app_icon.py`.

## Screen power

Brightness, night window and "screen off while the PC sleeps" are under Settings > Screen. The device dims at night by the PC's clock, turns the backlight off and stops drawing when the PC sleeps (USB suspend) or after `display.toggle`, and the first knob or button input only wakes the screen (30 s while the PC sleeps or with night brightness 0). The brightness knob changes the day or night level until the next save. Console: `screen`.

Firmware debug console (USB-UART port, 115200 baud): `help`, `link`, `media`, `stats`, `config`, `keys ctrl+shift+m` (send a shortcut from the device), `macro <id>`, `usb`, `sensor`, `enc`, `perf 1`, `view debug`, `bench`, ...
`tools/devconsole.py` sends commands from a script.

## Protocol test vectors

Weather icons: `python tools/gen_weather_icons.py [--preview sheet.png] --web web/img/weather` regenerates `firmware/src/widgets/weather_icons.c/.h` and the web editor's copies (needs Pillow).

`python tools/gen_protocol_vectors.py` regenerates `docs/protocol-test-vectors.json` and `firmware/test/test_protocol/vectors.h`. Both test suites use them.
