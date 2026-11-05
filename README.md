# DeskHub

USB desk companion: ESP32-S3 + 3.5" ILI9488 display, driven by a Windows tray app. See `CLAUDE.md` for the overview and `docs/` for specs.

## Requirements

- PlatformIO Core (`python -m pip install --user platformio`). Run it from **PowerShell or cmd**, not Git Bash.
- .NET 10 SDK.
- Optional, for firmware unit tests on the PC: a host C++ compiler (gcc/g++ on PATH, for example WinLibs MinGW-w64).

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
dotnet test --project tests/DeskHub.Protocol.Tests
dotnet run --project src/DeskHub.Host   # tray app, config page at http://127.0.0.1:47800
```

## Protocol test vectors

`python tools/gen_protocol_vectors.py` regenerates `docs/protocol-test-vectors.json` and `firmware/test/test_protocol/vectors.h`. Both test suites use them.
