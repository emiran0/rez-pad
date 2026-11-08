# Third-party notices

rez-pad's own code is under the MIT license (`LICENSE`). The released binaries also contain the
components below, each under its own license. Their full license texts come with the packages
(PlatformIO `firmware/.pio/libdeps`, NuGet) and on the linked project pages.

## Firmware (`rez-pad-firmware-<version>.bin`)

| Component | License | Project |
|---|---|---|
| Arduino-ESP32 core | LGPL-2.1-or-later | https://github.com/espressif/arduino-esp32 |
| ESP-IDF and its bundled components (FreeRTOS, TinyUSB, LittleFS and others) | Apache-2.0; bundled components under their own permissive licenses (FreeRTOS and TinyUSB: MIT) | https://github.com/espressif/esp-idf |
| LVGL | MIT | https://github.com/lvgl/lvgl |
| LovyanGFX | MIT and BSD-2-Clause | https://github.com/lovyan03/LovyanGFX |
| ArduinoJson | MIT | https://github.com/bblanchon/ArduinoJson |
| JPEGDEC | Apache-2.0 | https://github.com/bitbank2/JPEGDEC |
| Montserrat font (generated subsets) | SIL OFL 1.1 | https://github.com/JulietaUla/Montserrat |
| DejaVu Sans font (Greek subset) | Bitstream Vera license, DejaVu changes public domain | https://dejavu-fonts.github.io/ |
| Font Awesome 5 (LVGL's symbol subset) | SIL OFL 1.1 (font), CC BY 4.0 (icons) | https://fontawesome.com/ |

The Arduino-ESP32 core is linked statically. Under the LGPL you may rebuild the firmware with a
modified core: the complete firmware source is in this repository and builds with PlatformIO
(`firmware/platformio.ini`).

The weather icons and the app icon are original artwork made by `tools/gen_weather_icons.py` and
`tools/gen_app_icon.py` (MIT, like the rest of the project).

## Windows host (`rez-pad.exe`)

| Component | License | Project |
|---|---|---|
| .NET runtime, ASP.NET Core, Windows Forms, System.Management, System.IO.Ports | MIT | https://github.com/dotnet |
| LibreHardwareMonitorLib | MPL-2.0 | https://github.com/LibreHardwareMonitor/LibreHardwareMonitor |
| DiskInfoToolkit, RAMSPDToolkit (LibreHardwareMonitor dependencies) | MPL-2.0 | https://www.nuget.org/packages/DiskInfoToolkit, https://www.nuget.org/packages/RAMSPDToolkit-NDD |
| HidSharp | Apache-2.0 | https://www.zer7.com/software/hidsharp |
| Mono.Posix.NETStandard | MIT | https://github.com/mono/mono |

LibreHardwareMonitorLib and its MPL-2.0 dependencies are used unmodified; their source is
available from the projects above. The PawnIO driver that LibreHardwareMonitor uses is not
bundled; users install it themselves from https://pawnio.eu/.

Weather data comes from Open-Meteo (https://open-meteo.com/, CC BY 4.0 data, free for
non-commercial use).
