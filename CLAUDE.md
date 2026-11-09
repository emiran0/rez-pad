# rez-pad: USB Desk Companion (ESP32-S3 + Windows Host)

This file is the project overview and the rules for working on it. Read it fully before doing any work. Detailed specs live in `docs/`:

- `docs/hardware.md`: inventory, pin map, power, open hardware checks
- `docs/protocol.md`: USB protocol v1 (+ `docs/protocol-test-vectors.json`)
- `docs/config-schema.md`: config file, widget catalog, sources, actions, web editor, host API
- `docs/measurements.md`: real measurements from the hardware
- `docs/known-issues.md`: known limitations deliberately left for later (review before release)

See section 14 for decisions that changed or refined the original brief.

The project was renamed from the working name "DeskHub" to **rez-pad** on 2026-11-06. User-facing name: `rez-pad`. Code identifiers: `RezPad` (C#), `rezpad` / namespace `rp` (C++).

---

## 1. Project summary

A desk companion device connected to a Windows PC by a single USB-C cable. It has a 3.5" TFT, two clickable rotary encoders, optional buttons and a temperature/humidity/pressure sensor. It shows three (or more) customizable pages:

1. **Now Playing**: title, artist, album, album art, play state, progress bar with elapsed/duration, media controls, volume.
2. **System**: CPU/GPU load and temps, RAM, disks, network, fans, etc.
3. **Weather & Room**: current weather and forecast (from the PC), plus live room temperature, humidity, pressure and pressure trend (from the device sensor).

All pages are built from a **widget grid** and are customizable through a **browser-based config page** served by the Windows host app. A widget grid is the maximum level of customization. Free-form layout editing is explicitly out of scope.

### Goals
- Smooth, responsive, polished UI within the limits of an SPI display.
- Works fully over USB. No Wi-Fi setup required.
- Graceful offline mode when the host app is not running (clock, room climate, last weather, HID media keys still work).
- Easy to install and configure on Windows.

### Non-goals (for now)
- macOS or Linux host support.
- Touch input (the display has touch pins but no touch controller).
- Free-form drag-anywhere layouts.
- Device-side Wi-Fi features (may be added later as an optional mode).

---

## 2. Hardware (details in `docs/hardware.md`)

ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM, native USB + USB-UART bridge), 3.5" ILI9488 SPI TFT (480x320 landscape, no touch controller), AHT20 + BMP280 on I2C, 2 (up to 3) KY-040 encoders, 0 to 4 optional buttons.

Facts that drive the design (full list, pin map, power and open hardware checks in `docs/hardware.md`):
- ILI9488 over SPI is RGB666 only (3 bytes/pixel, ~460 KB per frame, ~90 ms full redraw at 40 MHz). No full-screen animations; page switches are instant redraws.
- Never connect the display's SDO/MISO (it does not tri-state). SD card, if used, goes on SPI3.
- KY-040 `+` goes to **3.3 V only**.
- Sensor must be away from heat sources; a calibration offset exists in config.

## 3. Pin map

Lives in `firmware/include/pins.h`, mirrored in `docs/hardware.md`. Change both or neither. Status: proposed, not yet verified against the physical board.


## 4. System architecture

```
+---------------------------- Windows PC -----------------------------+
|                                                                     |
|  rez-pad Host (C# / .NET, tray app, runs elevated at logon)         |
|   - MediaService     (GSMTC: now playing, art, controls, seek)      |
|   - AudioService     (Core Audio: master volume, mute)              |
|   - SystemStats      (perf counters + LibreHardwareMonitorLib)      |
|   - WeatherService   (Open-Meteo, no API key)                       |
|   - ConfigService    (master config JSON in %APPDATA%\rez-pad)      |
|   - WebServer        (Kestrel on 127.0.0.1, serves config editor)   |
|   - SerialLink       (COBS framed protocol over USB CDC)            |
|                                                                     |
|  Browser -> http://127.0.0.1:47800  (widget-grid config editor)     |
+------------------------------+--------------------------------------+
                               | USB-C (native USB port)
                               | composite device: CDC (data) + HID (consumer keys)
+------------------------------+--------------------------------------+
|  ESP32-S3 firmware                                                  |
|   - Link task      (USB CDC rx/tx, framing, heartbeat)              |
|   - UI task        (LVGL v9 on LovyanGFX, widget grid engine)       |
|   - Input          (PCNT encoders, buttons, gestures, action map)   |
|   - Sensors        (AHT20 + BMP280, filtering, pressure trend)      |
|   - HID            (consumer control fallback when host is absent)  |
|   - Config store   (LittleFS /config.json, validated, atomic)       |
+---------------------------------------------------------------------+
```

### Key design decisions
- **Host is the source of truth for config.** The device keeps a copy in flash so it boots straight into the user's layout even without the host. On connect, if the config CRCs differ, the host pushes its config.
- **The device owns input mapping.** The device resolves encoder/button events into **actions** (for example `media.volume -2`) using the config, then:
  - if the host is connected: sends an `ACTION` message to the host (host executes it precisely via GSMTC / Core Audio);
  - if the host is NOT connected: executes `media.*` actions as **HID consumer keys** (play/pause, next, prev, volume up/down, mute). Other actions run locally or are ignored.
- **The device interpolates the progress bar locally** from `pos_ms`, `dur_ms` and a host timestamp. The host does not stream position every second.
- **Album art is resized and JPEG-encoded on the host**, then decoded on the device into PSRAM.
- **Wi-Fi is disabled by default** on the device (less heat near the sensor, simpler setup).

---

## 5. Firmware

### Toolchain and libraries
- PlatformIO, Arduino framework on top of ESP-IDF. Platform: **pioarduino `platform-espressif32` 55.03.312-1** (Arduino-ESP32 core 3.3.12), pinned by release URL in `platformio.ini`. Pure ESP-IDF is acceptable if Arduino blocks something important; discuss before switching.
- Run `pio` from **PowerShell or cmd, not Git Bash**: pioarduino's tool installer refuses MSYS/MinGW shells and the build then fails with "xtensa-esp32s3-elf-g++ is not recognized".
- **LovyanGFX**: display driver (`Panel_ILI9488`, `Bus_SPI` on `SPI2_HOST`, DMA). Handles RGB565 to RGB666 conversion.
- **LVGL v9**: UI. `LV_COLOR_DEPTH 16`.
- **JPEGDEC** (bitbank2): album art decoding into PSRAM.
- **ArduinoJson v7**: config and JSON message bodies.
- **LittleFS**: config and assets.
- ESP-IDF **pulse_cnt** driver for encoders (hardware quadrature counting with glitch filter).
- Arduino TinyUSB classes: `USBCDC` + `USBHIDConsumerControl` as a composite device.
- AHT20 and BMP280: use small, well-maintained libraries or write minimal drivers (both are simple).

**Always verify current stable versions of the platform and every library before pinning them in `platformio.ini`. Do not rely on memorized versions.**

### Board configuration (verify flags against the current core)
- Board: **`esp32-s3-devkitc1-n16r8`** (ships with pioarduino: 16 MB QIO flash, `qio_opi`, `BOARD_HAS_PSRAM`), so no manual overrides are needed.
- USB mode: **USB-OTG (TinyUSB)**, so the native port can be CDC + HID.
- `Serial` = UART0 (the bridge USB-C port) for **debug logs only**. The protocol runs on the native USB CDC.
- Custom USB descriptors: manufacturer "rez-pad", product "rez-pad", a stable serial string. Host discovers the port by VID/PID and product string.
- Custom partition table (`firmware/partitions.csv`): 6 MB factory app (no OTA), ~9.9 MB LittleFS (label `spiffs`, the `LittleFS.begin()` default), 64 KB coredump.
- Note: once TinyUSB takes over the native port, flashing through it may require holding BOOT. The UART port is always available for flashing.

### Task layout (FreeRTOS)
- **UI task** (core 1): owns LVGL. All LVGL calls happen here only. Other tasks post messages through a queue.
- **Link task** (core 0): USB CDC read, COBS decode, CRC check, dispatch; outgoing queue; heartbeat.
- **Input**: PCNT polling plus switch sampling at ~1 ms (esp_timer or a small task). Emits gesture events.
- **Sensor task**: reads every 5 s.

### Display
- Landscape 480x320.
- Start SPI write clock at **40 MHz**. Add a debug command to try 60 and 80 MHz and record results in `docs/measurements.md`.
- LVGL partial rendering, two draw buffers of 480 x 40 lines in **internal DMA-capable RAM** (not PSRAM).
- Backlight via LEDC PWM, brightness 0 to 100 from config, night dimming schedule, off on USB suspend (PC sleep), wake on any input or USB resume.
- Layout: a **20 px status bar** (clock, page indicator dots, host connection icon) plus a **grid area of 480x300** split into **4 columns x 3 rows** (cells 120x100 px). Grid dimensions come from config but 4x3 is the default.
- Page switches are instant (no slide animation). Small fades on individual widgets are fine.

### Fonts
- LVGL built-in Montserrat lacks many non-ASCII glyphs. Track titles can contain any language. Generate fonts with `lv_font_conv` covering at least **Basic Latin, Latin-1 Supplement, Latin Extended-A** (covers most European languages including Turkish) plus a symbol/icon font.
- Stretch: Cyrillic and Greek. CJK is out of scope for built-in fonts; render missing glyphs as a placeholder and do not crash.

### Input
- Encoders: PCNT with glitch filter (~1 us), configurable `counts_per_detent` (KY-040 is usually 2 or 4), configurable direction invert.
- Switches and buttons: 20 ms debounce. Gestures: `click`, `double` (< 300 ms between clicks), `long` (> 600 ms hold).
- Press-and-turn: turning an encoder while its knob is held is a separate gesture (`rotate_pressed`); that press then produces no click/long.
- Events map to actions through the config (global map plus optional per-page overrides).

### Sensors
- AHT20 at 0x38. BMP280 at 0x76 or 0x77 (probe both).
- Read every 5 s. Apply a short median filter plus EMA. Apply user offsets (`temp_offset_c`, `hum_offset_pct`).
- Report AHT20 temperature as the room temperature (BMP280 temperature is for diagnostics only).
- Pressure trend from a 3-hour ring buffer: rising / falling / steady (threshold configurable, default 1.0 hPa per 3 h).
- Send `SENSOR` messages to the host every 10 s so the config page can show live readings.

### Offline behavior (host not connected for > 3 s)
- Status bar shows a disconnected icon.
- Clock continues from the last time sync (keep time with the ESP32 system clock).
- Room climate widgets stay live.
- Weather widgets show last data with its age ("updated 2 h ago"). Hide after 12 h.
- Now Playing shows "No host connected". System widgets show "--".
- `media.*` actions go out as HID consumer keys.

### Robustness
- Config validation on load. If invalid, fall back to the built-in default config and show a small warning on screen.
- Atomic config writes (write `/config.tmp`, verify, rename).
- Task watchdog enabled. Malformed frames are dropped and counted, never crash.

---

## 6. Windows host app

### Stack
- **C# on .NET 10 (current LTS)**, target framework `net10.0-windows10.0.19041.0` for WinRT APIs.
- Tray app (WinForms `NotifyIcon` is enough) plus **ASP.NET Core Kestrel** bound to `127.0.0.1:47800` (port configurable).
- `System.IO.Ports` for serial.
- `Windows.Media.Control` (GSMTC) for media.
- Core Audio for volume via a small direct COM interop wrapper (`IAudioEndpointVolume`), no NAudio dependency.
- **LibreHardwareMonitorLib** for temps, fans, clocks, GPU. Check the current release notes for its kernel driver requirements (recent builds moved from the WinRing0 driver to PawnIO). Detect missing driver/admin and show guidance in the tray menu and web UI.
- `Windows.Graphics.Imaging` (built-in WinRT) for decode/resize/JPEG encode of album art. Avoid image libraries with restrictive licenses.

**Verify current stable versions of all NuGet packages before adding them.**

### Solution layout
```
host/
  RezPad.slnx
  src/RezPad.Host/          tray app, services, Kestrel, web assets
  src/RezPad.Protocol/      COBS, CRC, message types (no Windows deps, unit-testable)
  tests/RezPad.Protocol.Tests/
```

### Services
- **SerialLink**: find the device by USB VID/PID and product string (WMI or SetupAPI), open, reconnect loop with backoff, heartbeat. Handles unplug/replug within 3 s.
- **MediaService**: `GlobalSystemMediaTransportControlsSessionManager`. Subscribe to `CurrentSessionChanged`, `MediaPropertiesChanged`, `PlaybackInfoChanged`, `TimelinePropertiesChanged`. Optional app priority list in config (for example prefer Spotify over browser tabs). Executes play/pause, next, prev, seek (`TryChangePlaybackPositionAsync`, may be unsupported by some apps; report `can_seek`).
- **ArtProcessor**: open thumbnail stream, center-crop to square, resize to the size requested by the active layout (default 200x200), encode JPEG (quality ~85). Cache last N by `track_id`. Skip resending art the device already has.
- **AudioService**: master endpoint volume and mute, event-driven change notifications, executes relative volume actions with configurable step.
- **SystemStatsService**: CPU load, RAM, disk usage, network throughput via performance counters/PDH. Temps, fans, clocks, GPU via LibreHardwareMonitorLib. Exposes a **catalog of available data sources** (stable IDs plus display names) to the web UI. Sends only the sources referenced by the current config, at the configured rate (default 1 Hz, max 4 Hz).
- **WeatherService**: Open-Meteo forecast API (no key) plus Open-Meteo geocoding for the city search in the web UI. Refresh every 15 min. Metric/imperial from config.
- **ConfigService**: `%APPDATA%\rez-pad\config.json`, JSON schema validation, versioned, pushes to device on change or CRC mismatch.
- **WebServer**: serves `web/` static files plus a REST/WebSocket API (see section 8).

### Startup and elevation
- Temps need admin. GSMTC only works inside the user session, so the host **cannot be a Windows service**.
- Solution: register a **Task Scheduler task at user logon with "Run with highest privileges"**. Provide a tray menu item "Start with Windows" that creates/removes this task.
- Without admin the app still runs; temps/fans show as unavailable.

### Resource targets
- Average CPU < 1%, memory < 150 MB.

---

## 7. USB protocol

Specified in `docs/protocol.md` (v1). Summary: COBS-framed packets `[ver][type][seq u16][body][crc16]` over the native USB CDC port, JSON bodies for small messages, binary chunks for art and config. Shared test vectors in `docs/protocol-test-vectors.json` (generated by `tools/gen_protocol_vectors.py`) are used by both the C# and C++ unit tests.

## 8. Configuration

Specified in `docs/config-schema.md` (schema 1): config file layout, widget catalog with allowed sizes, data source IDs, actions, web editor features and the localhost host API.


## 9. Performance targets

| Metric | Target |
|---|---|
| Track change to text on screen | < 150 ms |
| Track change to album art on screen | < 600 ms |
| Encoder input to host action executed | < 100 ms |
| Full page redraw at 40 MHz SPI | < 120 ms |
| Device recovers after host restart | < 3 s |
| Host recovers after device unplug/replug | < 3 s |
| Host average CPU | < 1% |

Record real measurements in `docs/measurements.md`.

---

## 10. Repository layout

```
rez-pad/
  CLAUDE.md
  VERSION            the release version (docs/versioning.md)
  CHANGELOG.md
  docs/
    hardware.md        pin map, wiring, power, photos
    protocol.md        protocol v1 (moved from this file)
    config-schema.md   config schema and widget catalog
    measurements.md    SPI speed tests, timings, sensor calibration
    protocol-test-vectors.json  shared by C# and C++ protocol tests
    default-config.json         built-in default config (host embeds it, firmware header generated)
    config-test-vectors.json    config validation cases shared by C# and C++ tests
  firmware/
    platformio.ini
    partitions.csv
    include/pins.h     single source of truth for pins (mirrors docs/hardware.md)
    src/
      main.cpp
      link/            USB CDC transport, dispatch, heartbeat
      hid/             consumer control fallback
      display/         LovyanGFX setup, backlight
      ui/              LVGL setup, status bar, page manager
      widgets/         one file per widget type
      input/           encoders, buttons, gestures, action mapping
      sensors/         AHT20, BMP280, filtering, trend
      config/          load, validate, save, defaults
      fonts/           generated LVGL fonts
    lib/protocol/      pure C++ COBS/CRC/framing (no Arduino deps, host-testable)
    lib/config_logic/  pure C++ config parsing and validation (ArduinoJson only, host-testable)
    test/              PlatformIO native tests (protocol, config validation)
  tools/
    gen_protocol_vectors.py   reference framing implementation, generates shared test vectors
    gen_config_headers.py     C headers from docs/default-config.json and config-test-vectors.json
    gen_app_icon.py           app, tray and favicon icons
    package.ps1               release zip (single-file host, setup scripts, firmware image)
  setup/
    install.ps1, Install.cmd, Uninstall.cmd, README.txt   per-user installer shipped in the zip
  host/
    RezPad.slnx
    src/RezPad.Host/
    src/RezPad.Protocol/
    tests/RezPad.Protocol.Tests/
  web/
    index.html
    app.js, state.js, editor.js, preview.js, settings.js
    styles.css
    img/weather/       weather icons for the preview (tools/gen_weather_icons.py --web)
```

---

## 11. Roadmap (phases with acceptance checks)

Work phase by phase. At the end of each phase, list what was done and what the user should verify on real hardware.

**Phase 0: Scaffold**
- Create the repo layout, split this file's protocol/config/hardware sections into `docs/` (keep this file as the overview and rules).
- Firmware builds and logs "hello" on UART0. Host solution builds. Web folder served by a stub.
- Check: toolchain works, board flashes, PSRAM detected (log size).

**Phase 1: Display bring-up**
- LovyanGFX on ILI9488, landscape, backlight PWM.
- Benchmark command: full-screen fill and a 200x200 region at 40/60/80 MHz. Record in `docs/measurements.md`.
- Check: stable image, no artifacts, chosen default clock.

**Phase 2: Inputs and sensor**
- PCNT encoders with detent scaling, switch gestures, optional buttons.
- AHT20 + BMP280 readings with filtering and offsets. On-screen debug view.
- Check: no missed or double steps when turning fast or slow; correct gestures; plausible sensor values.

**Phase 3: LVGL and static UI**
- LVGL v9 integrated, status bar, page manager, fonts with extended Latin.
- Hardcoded mock data for the three default pages.
- Check: page switching with encoder 1, partial updates smooth, special characters render.

**Phase 4: USB composite device**
- TinyUSB CDC + HID consumer control on the native port with custom descriptors.
- Check: Windows shows a COM port and a HID device; encoder 2 click toggles media playback with no host app running.

**Phase 5: Protocol and host skeleton**
- `RezPad.Protocol` with COBS, CRC16, CRC32, message types, unit tests.
- Firmware link task with the same framing.
- Host tray app: device discovery, handshake, heartbeat, reconnect, time sync.
- Check: unplug/replug and host restart both recover within 3 s; clock shows correct local time.

**Phase 6: Now Playing**
- MediaService (GSMTC), ArtProcessor, AudioService, ACTION handling (play/pause, next, prev, seek, volume, mute).
- Firmware widgets: `album_art`, `track_info` (marquee), `media_controls`, `progress`, `volume`. Local progress interpolation.
- Check: Spotify and a browser tab both work; art appears within target; seeking works where supported; HID fallback still works when the host is closed.

**Phase 7: System stats**
- SystemStatsService with source catalog, perf counters, LibreHardwareMonitorLib.
- "Start with Windows" scheduled task with highest privileges. Clear UI when temps are unavailable.
- Firmware widgets: `metric_value`, `metric_bar`, `metric_gauge`, `metric_graph`.
- Check: values match Task Manager / a reference tool; host CPU usage within target.

**Phase 8: Weather and room**
- WeatherService (Open-Meteo) and geocoding.
- Firmware widgets: `weather_now`, `weather_forecast`, `room_climate`, `clock`. Weather icon set for WMO codes (original icons, no copied icon packs with restrictive licenses).
- Pressure trend.
- Check: weather matches the Open-Meteo website; offline mode shows data age.

**Phase 9: Widget grid engine**
- Firmware builds pages entirely from config JSON with validation and default fallback. Per-page input overrides.
- Config transfer (CONFIG_BEGIN/CHUNK/END/ACK, CONFIG_GET), atomic save.
- Check: invalid config never bricks the UI; device boots into the saved layout without the host.

**Phase 10: Web config editor**
- Grid editor, palette, properties, inputs, settings, live status, preview, save and push.
- Check: every widget type can be added, resized within allowed sizes, configured and shown on the device.

**Phase 11: Polish and packaging**
- Night dimming, sleep on PC sleep, wake on input.
- Host single-file publish plus a simple installer or setup script; "Start with Windows" toggle.
- Optional: ESP Web Tools page for flashing firmware from the browser.
- Check: fresh Windows machine setup in under 5 minutes.

**Phase 12: Extras** (the former stretch list, without the SD card)
- `spectrum` widget (WASAPI loopback capture + FFT on host, 32 bins at ~30 Hz).
- Per-app volume control: an interactive `mixer` widget (the Windows volume mixer; focus it with a button, knob 1 picks an app, knob 2 sets its volume), plus knob actions for the playing app's volume.
- Optional device Wi-Fi mode for standalone weather (last, only if still wanted).
- Check: the spectrum follows the music without stutter and costs the host < 1 % CPU; per-app volume works from a knob.

**Phase 13: External integrations** (homelab apps the host reads for the device; full support per app, no user-written schemas)
- Integrations framework on the host: host-only settings with DPAPI-encrypted credentials, a test button, sources and/or feeds per app.
- Beszel (system monitoring): machines as data sources `bz.<machine>.<metric>` for every metric widget, plus a `server` card widget.
- ntfy (notifications): last N notifications as a widget (new message type), optional popup.
- Check: values match the Beszel UI; credentials never reach the device; host CPU stays < 1 %.

Dropped: SD card on SPI3 (decided with the user 2026-11-08; the 16 MB flash is enough for art, images and fonts).

---

## 12. Rules for working on this project

1. **Never use the em-dash character (U+2014) anywhere**: code, comments, docs, commit messages, UI strings, log messages. Also avoid the en-dash (U+2013). Use commas, colons, parentheses or a plain hyphen instead. This is a hard user preference.
2. **Verify current versions** of PlatformIO platforms, the Arduino-ESP32 core, LVGL, LovyanGFX, .NET and every library or NuGet package before adding or pinning them. Do not rely on memory.
3. **Do not guess hardware details.** If something depends on the physical board or module (pin labels, onboard LED pin, display VCC jumper, backlight transistor, encoder detent count), ask the user or add a measurement step.
4. **Pins live in one place**: `firmware/include/pins.h`, mirrored in `docs/hardware.md`. Change both or neither.
5. **Protocol changes** go into `docs/protocol.md` first, are implemented on both sides in the same change, and bump `PROTOCOL_VERSION` if breaking.
6. **Config schema changes** bump `schema` and include a migration on the host.
7. Keep the firmware non-blocking. LVGL is only touched from the UI task.
8. Write unit tests for protocol encoding/decoding and config validation on both sides.
9. Windows is the only supported host OS for now. Keep OS-specific code isolated so other hosts could be added later.
10. Keep commits small and scoped to one phase or feature. End every phase with a short summary and a hardware checklist for the user.
11. **Versions follow docs/versioning.md**: one version in the `VERSION` file for host, firmware and installer (never written anywhere else); MINOR for a big update (a finished phase or notable feature set), PATCH for fixes only, MAJOR after 1.0 for breaking changes. Bump only when making a release: `VERSION`, a `CHANGELOG.md` entry, a `vX.Y.Z` tag, then `tools/package.ps1`.

---

## 13. Open questions

Tracked in the "Open hardware checks" table in `docs/hardware.md` (native USB port, RGB LED pin, display VCC/J1, backlight transistor, encoder detent count, buttons, enclosure). Ask the user when a phase depends on one, and record the answer there.

---

## 14. Decisions log (changes to the original brief)

- 2026-11-06: Protocol framing lives in a pure C++ library (`firmware/lib/protocol/`) instead of `src/link/`, so it can be unit-tested on the host. `src/link/` keeps only the USB transport and dispatch.
- 2026-11-06: Shared protocol test vectors (`docs/protocol-test-vectors.json`, plus a generated C header for the firmware tests) produced by `tools/gen_protocol_vectors.py`. Both sides test against the same bytes, so they cannot drift.
- 2026-11-06: Protocol details pinned down: CRC-16/CCITT-FALSE, CRC-32/ISO-HDLC (zlib), canonical COBS, per-side `seq` counters, `track_id` = CRC32 of `title + "\n" + artist + "\n" + album`. See `docs/protocol.md`.
- 2026-11-06: Board definition `esp32-s3-devkitc1-n16r8` from pioarduino instead of overriding `esp32-s3-devkitc-1`.
- 2026-11-06: Config `inputs.buttons` (0 to 2) added to schema 1 (not yet released, so no migration) to declare how many physical buttons are fitted. Default 0.
- 2026-11-06: Host uses the `.slnx` solution format (.NET 10 default) and xUnit v3 on Microsoft.Testing.Platform (`host/global.json` opts in; `dotnet test --project tests/RezPad.Protocol.Tests`). The .NET 10 SDK no longer runs MTP-based tests through VSTest.
- 2026-11-06: Volume control via direct Core Audio COM interop instead of NAudio (smaller, no extra dependency).
- 2026-11-06: Project renamed from "DeskHub" to **rez-pad**. C# projects/namespaces `RezPad.*`, host exe `rez-pad.exe`, C++ library `rezpad_protocol` in namespace `rp::proto`, PlatformIO env `rezpad`, config folder `%APPDATA%\rez-pad`.
- 2026-11-07: SPI display clock steps are 80 MHz / n on the ESP32-S3 (80, 40, 26.7, 20 MHz). "60 MHz" in the Phase 1 plan is not reachable; it runs at 40 MHz. The benchmark reports requested and effective clocks.
- 2026-11-07: LovyanGFX pinned to 1.2.36. 1.3.0 was released 2026-11-06 with breaking DMA/sprite changes; revisit when integrating LVGL (Phase 3).
- 2026-11-07: Debug console on UART0 (`firmware/src/debug/`) with `tools/devconsole.py` to drive it. Used for bring-up and measurements until the protocol CMD message exists (Phase 5).
- 2026-11-07: Default `counts_per_detent` stays 4, confirmed by a 10-click test on the fitted KY-040 (an earlier pin-level reading suggested 2; that was wrong).
- 2026-11-07: Humidity is corrected for the temperature offset instead of only adding `hum_offset_pct`: the sensor runs warm (self-heating), so its RH reads low. RH is converted from the measured to the corrected temperature with the Magnus formula (same absolute humidity), then `hum_offset_pct` is added.
- 2026-11-07: BMP280 runs in forced mode (one conversion per 5 s read, no IIR), so it sleeps between reads and adds no heat. Filtering is done in firmware (median of 3, then EMA).
- 2026-11-07: Input, gesture and sensor maths live in pure C++ libraries (`firmware/lib/input_logic`, `firmware/lib/sensor_logic`) with native tests, including the BMP280 datasheet compensation example.
- 2026-11-07: Pressure trend needs 1 h of history before reporting, and extrapolates to 3 h until 3 h of data exist.
- 2026-11-07: Press-and-turn gesture (`rotate_pressed`) added to the encoder mapping. Default: enc1 turn = pages, enc1 press+turn = seek; enc2 turn = volume, enc2 press+turn = brightness. Replaces the default Now Playing override of `enc1.rotate` to `media.seek`, which made it impossible to leave that page by turning.
- 2026-11-07: Fonts come from sources bundled in the LVGL package (Montserrat Medium for Latin, Latin Extended-A and Cyrillic; DejaVu Sans for Greek; LVGL's FontAwesome symbol subset), generated by `tools/gen_fonts.py` with lv_font_conv 1.5.3 at 14/20/28 px, plus 48 px ASCII for big numbers. Missing glyphs (CJK) draw as placeholder boxes (`LV_USE_FONT_PLACEHOLDER`).
- 2026-11-07: LVGL 9.6 uses `LV_COLOR_FORMAT_DEFAULT` (RGB565) instead of the deprecated `LV_COLOR_DEPTH`, and dedicated setters (`lv_obj_set_hidden()` etc.) instead of `lv_obj_add_flag()`.
- 2026-11-07: Widgets are grouped by family (`widgets/media_widgets.cpp`, `metric_widgets.cpp`, `weather_widgets.cpp`) instead of one file per type, so related widgets share helpers. Factory and size table in `widgets/factory.cpp`.
- 2026-11-07: Only track titles scroll (marquee, 1 px per frame); other long text ends with "...". Measured: three scrolling labels cost 50-85 % of the UI core, one costs about 40 %. LVGL refresh capped at 30 FPS.
- 2026-11-07: Firmware layout additions: `src/app/` (action mapping), `src/ui/` also holds the data model, demo data (`mock.cpp`, until the host link exists) and the built-in default layout (`default_layout.cpp`, mirrors the default config until Phase 9 loads it from flash).
- 2026-11-07: USB identity 303A:4005 (TinyUSB's pre-allocated PID for CDC + HID under the Espressif VID; Espressif's usb-pids rules say no custom PID is needed when only standard classes are used). Not 303A:1001, which the chip's built-in USB-Serial/JTAG uses. Discovery: VID/PID + product "rez-pad" + serial "RP-<MAC>" (`docs/protocol.md`).
- 2026-11-07: HID media keys are sent from their own task (`src/hid/media_keys.cpp`) through a queue, so USB report timing never blocks the UI loop. Offline, `media.seek` is ignored (no reliable consumer key); volume sends one key per detent (each is one Windows volume step).
- 2026-11-07: `firmware/scripts/fs_include.py` adds FS's include path for Arduino's USB library; PlatformIO does not honour that framework library's `depends=FS`, and `${platformio.packages_dir}` in build_flags loses its backslashes on Windows.
- 2026-11-07: The CDC port has Arduino's reboot-to-bootloader triggers (esptool DTR/RTS sequence, 1200 baud) **disabled**: .NET SerialPort raises RTS before DTR, which is the first step of that sequence, and a close in the wrong order would reboot the device into the ROM bootloader. Flashing stays on the USB-UART port (or BOOT + RESET on the native port).
- 2026-11-07: Host serial I/O uses a small Win32 wrapper (`Device/Win32Serial.cs`: CreateFile, COMMTIMEOUTS, DTR/RTS via DCB) instead of System.IO.Ports: SerialPort runs a hidden WaitCommEvent thread, threw OperationCanceledException on unplug (crashed the first version), and could not guarantee the handle closes. No NuGet dependency for serial anymore.
- 2026-11-07: Device discovery reads the PnP tree from the registry (composite `USB\VID_303A&PID_4005\RP-*` -> ParentIdPrefix -> `MI_01` child -> PortName) and checks `HARDWARE\DEVICEMAP\SERIALCOMM` for presence. No WMI, no admin rights.
- 2026-11-07: Link state logic is pure and unit-tested on both sides: `firmware/lib/protocol/src/link_monitor.*` (device: HELLO, 3 s PING timeout, `need_hello`) and `RezPad.Protocol/HeartbeatMonitor.cs` (host: PING 1 s, lost after 3 s without PONG, TIME_SYNC every 60 s).
- 2026-11-07: Until Phase 6, the host executes ACTION messages by injecting Windows media keys (`Media/MediaKeyFallback.cs`); media.seek is logged as not supported yet.
- 2026-11-07: Host extras: `rez-pad.exe --exit` asks the running instance to quit cleanly (installers will need it), `GET /api/log` returns recent log lines, `POST /api/device/cmd` sends CMD messages. Log file: `%LOCALAPPDATA%\rez-pad\host.log`.
- 2026-11-07: The status bar clock shows real local time once the host has sent TIME_SYNC and `--:--` before that; the demo clock is gone.
- 2026-11-07: Phase 6 host layout: `Media/MediaService.cs` (GSMTC, session choice: playing first, then `media.app_priority`, then Windows' current session), `Media/ArtProcessor.cs`, `Media/MediaBridge.cs` (link <-> services), `Audio/AudioService.cs` + `Audio/CoreAudio.cs`. `MediaKeyFallback` stays for play/pause/next/prev when there is no GSMTC session. Media options use defaults until ConfigService exists (Phase 9/10). `GET /api/media` shows what the host sends.
- 2026-11-07: Protocol JSON is written with raw UTF-8 (`UnsafeRelaxedJsonEscaping`); the default `\uXXXX` escaping made non-Latin titles up to 6x larger. Only the device reads it.
- 2026-11-07: HELLO_ACK gained an optional `art` field (album art edge the layout wants, 192 px for the default 2x2 card); additive, so still protocol v1. Config gained `media.seek_step_s` (default 5; schema 1 unreleased, no migration). The host applies volume and seek steps; the device moves its volume/position display optimistically by the same defaults.
- 2026-11-07: Album art: `firmware/lib/protocol/src/art_assembler.*` (pure, native tests) reassembles; `src/media/` decodes with JPEGDEC 1.8.5 on the link task into one of two 240x240 RGB565 PSRAM buffers and hands it to the UI (the UI never shows the buffer being written). The previous image stays up to 1 s after a track change before the placeholder appears. Device sends ART_REQUEST once per track if art is missing after 2.5 s.
- 2026-11-07: Mock Now Playing and volume removed; offline the device shows "No host connected" and volume "--". System stats and weather stay mock until Phases 7 and 8.
- 2026-11-07: Phase 7 host layout: `Stats/SystemStatsService.cs` (source catalog, own sampling thread), `Stats/HardwareSensors.cs` (LibreHardwareMonitorLib 0.9.6, MPL-2.0; PawnIO driver since 0.9.5, installed separately from pawnio.eu), `Stats/StatsBridge.cs` (SYS_STATS), `StartupTask.cs` ("Start with Windows"). CPU load/clock come from Windows counters so they match Task Manager; LibreHardwareMonitor is only for temperatures, GPU and fans.
- 2026-11-07: `sys.gpu.load` = busiest D3D engine (Task Manager's definition), not the driver's "GPU Core" load, which ignores video decode/encode; the latter is `sys.gpu.core_load`. The stats thread refreshes only the hardware groups the layout's sources need (`Needs` flags), and all of them for 5 s after `/api/sources` or `/api/hardware` is called. `GET /api/hardware` lists every LibreHardwareMonitor sensor.
- 2026-11-07: GPU values are read directly: `Stats/Nvml.cs` (nvml.dll from the NVIDIA driver) and `Stats/GpuEngines.cs` (D3DKMT engine statistics, any vendor, the same data Task Manager uses), matched by PCI address. LibreHardwareMonitor's GPU part costs about 33 ms CPU per refresh, so it is only opened when NVML or D3DKMT is missing, or when the layout uses `sys.gpu.hotspot_temp`. Host CPU 0.29 % to 0.10 %. D3DKMT structs are hand-written; sizes were checked against LibreHardwareMonitor's CsWin32 types (no CsWin32 dependency, its WDK metadata is experimental only).
- 2026-11-07: Phase 8: host `Config/ConfigStore.cs` (JSON-node based, owns only `weather` and `units` so far, keeps other keys, atomic writes), `Weather/WeatherService.cs` (Open-Meteo forecast every `refresh_min`, retry 1/2/5/10 min, geocoding), `Weather/WeatherBridge.cs`. API: `GET/PUT /api/weather`, `GET /api/geocode?q=`, `GET /api/sensor`. Default config has no weather location (no "Example City" coordinates); the user picks one on the web page.
- 2026-11-07: WEATHER `units` is an object (`temp`, `wind`, `pressure`) instead of the string "metric"; `now.press`, per-hour `is_day` added (unreleased, still v1). SENSOR gained `delta_3h` and nullable readings. The device takes its display units from WEATHER until the config store exists, and converts its room readings with them.
- 2026-11-07: Weather icons are original artwork drawn by `tools/gen_weather_icons.py` (Pillow, 8x supersampled) into `firmware/src/widgets/weather_icons.c`: 12 icons (WMO groups, day/night) at 32/48/72 px, RGB565A8, about 300 KB of flash.
- 2026-11-07: All firmware mock data is gone (`ui/mock.*` deleted, no DEMO badge). Weather lives in RAM only: after a reboot without the host there is none; while the host is away it shows its age and disappears 12 h after the fetch. New widget `clock` (1x1, 2x1).
- 2026-11-07: Motherboard fan ids follow the SuperIO header number (`sys.fan.3.rpm` = "Fan #3"), and every header is listed even at 0 rpm, so ids stay stable when a fan is stopped at startup.
- 2026-11-07: When debugging from the Claude desktop app, read the host log through `GET /api/log`: the app is MSIX-packaged and its child processes see a private copy of `%LOCALAPPDATA%`, so the elevated (Task Scheduler) host's real `host.log` is invisible there.
- 2026-11-07: LibreHardwareMonitor reports unreadable CPU sensors as 0 without PawnIO/admin; temperatures and CPU power of 0 are treated as unavailable. Unavailable referenced sources go out as `null` in SYS_STATS and show `--`.
- 2026-11-07: "Start with Windows" relaunches the exe elevated (`--register-startup "<user>"` / `--unregister-startup`, one UAC prompt) to create or delete the `rez-pad` task (logon trigger, highest privileges, 5 s delay). It can restart into the task right away; a new instance waits up to 10 s for the old one's single-instance mutex, and the old one frees the HTTP port before releasing it.
- 2026-11-07: Until the config exists (Phase 9) the host sends the default layout's sources (`StatsBridge.DefaultLayoutSources`) at 1 Hz. Firmware mock stats removed (`src/sysstats/` receives SYS_STATS); only weather is still demo data.
- 2026-11-07: Keep the native USB cable off the Genesys USB 2.0 hub (`05E3:0610`): with both board USB devices behind it, the 30 s COM close / COM4 wedge came back (see `docs/measurements.md`).
- 2026-11-07: Phase 9: one default config, `docs/default-config.json`. The host embeds it, `tools/gen_config_headers.py` generates the firmware's copy; the hand-written firmware layout (`ui/default_layout.cpp`) and `config/defaults.h` are gone. Default labels "CPU temp"/"GPU temp" (temperature widgets now get the unit "°C" from the source).
- 2026-11-07: Config validation is implemented on both sides (`RezPad.Protocol/Config.cs`, `firmware/lib/config_logic`) and tested against the same cases (`docs/config-test-vectors.json`); errors are `"<path>: <reason>"` with identical paths. The action enum and names moved into `config_logic`. Rotate actions are only allowed on rotate gestures and press actions only on presses. Limits: 8 pages, 16 widgets per page, 16 different sources, 32 KB. Metric style fields default from the source id (`cfg::sourceDefaults`). `spectrum` is left out of the validators until it exists.
- 2026-11-07: The config's identity is the CRC32 of its compact UTF-8 bytes; the host pushes when HELLO_ACK `cfg_crc` differs (0 = nothing stored) and after every saved change, the device stores exactly those bytes. A pushed config is validated and saved on the link task, then the UI task rebuilds all pages and sends CONFIG_ACK (with the new layout's `art` size, additive in v1). CONFIG_GET and CMD `factory_reset` implemented.
- 2026-11-07: Host `Config/ConfigStore.cs` became `ConfigService.cs` (load, migrate by filling missing sections/keys from the default, validate, atomic save, `.bad` backup). `Device/ConfigSync.cs` does push and readback. Media options, stats sources and `stats.rate_hz` come from the config; weather refetches only when location or units change. New API: `GET/PUT /api/config`, `GET /api/config/default`, `GET /api/device/config`; `/api/status` has the sync state.
- 2026-11-07: Page `inputs_override` is merged into a full per-page input map at parse time; the action handler uses the current page's map. Theme `accent` is applied (status bar dots, gauges, graphs). New widget `label` (`text`, optional `label`).
- 2026-11-08: Phase 10 web editor without gridstack.js: the grid has at most 8x8 cells and each widget type a short list of allowed sizes, so a small own editor (drag to move, size picked from the allowed list) fits better than a free-resize library, and there is nothing to vendor. The editor is also the preview: cards are drawn like the device (`web/preview.js`) with live data. Plain ES modules, no build step.
- 2026-11-08: The editor polls instead of a WebSocket (`/ws` dropped from the plan): localhost, a handful of small requests per second, simpler host. Validation runs on the host only (`POST /api/config/validate`), so the web page has no third copy of the rules. `PUT /api/config?base=<crc>` refuses to overwrite a config that changed since the page loaded.
- 2026-11-08: LVGL allocates from PSRAM (`LV_STDLIB_CUSTOM`, `src/ui/lv_mem_psram.c`). With malloc every LVGL object went to internal RAM: 8 pages of 12 widgets left 3 KB; now 115 KB. Measured page redraw time is the same (about 153 ms either way, the SPI transfer dominates). Draw buffers stay in internal DMA RAM.
- 2026-11-08: Console `info` shows the internal heap low-water mark, uptime and the reset reason; `page <n>` reports the redraw time.
- 2026-11-08: Widget sizes are no longer a fixed list per type: any size whose content area is at least the type's minimum (px) is allowed, on any grid (`WidgetMin` / `kTypes` in the validators). Every firmware widget adapts its fonts and parts to its size; `web/preview.js` mirrors the same rules. The gauge draws the number sized to the ring and the unit separately (it overflowed before). Media widgets are built from shared views (`ArtView`, `TextView`, `ControlsView`), which also make the new combined `now_playing` widget. The layout's album art size is the largest any art view wants; smaller ones scale it.
- 2026-11-08: Pages have their own `grid` and `theme` (`{name, accent}`), falling back to the global ones (schema 1 is unreleased, so no migration). Four theme presets (`dark`, `midnight`, `black`, `graphite`); the firmware keeps a palette per page and makes it current while that page builds and refreshes.
- 2026-11-08: `image` widget with uploaded pictures and animated GIFs. The host keeps originals in `%APPDATA%\rez-pad\assets` and converts them with GDI+ (System.Drawing, part of the Windows desktop runtime, composites GIF frames) into RPA1 files (JPEG frames with delays) per widget box and card color; new protocol messages ASSET_BEGIN/CHUNK/END/LIST and ASSET_ACK/LIST_DATA (additive within v1). The device stores them in LittleFS `/a/`, decodes all frames into PSRAM when the widget is built and flips them with an LVGL timer; files no widget uses are deleted after each config. Budgets: 1 MB per file, 2 MB decoded per widget, 4 MB in total, at least 50 ms per frame.
- 2026-11-08: Editor redesign: Layout / Settings / Device views; page rail with live thumbnails; the screen is the editor (drag to move or swap, corner handle to resize, click an empty cell to add); inspector with a widget gallery (live previews, click or drag) and the selected widget's settings; undo/redo history; validation banner with "Show me".
- 2026-11-08: Macro buttons: up to 4 tact switches (GPIO 17, 18, 8, 13; `inputs.buttons` 0 to 4) with click, double and long. A press gesture (also knob clicks) takes an action name, `{"keys": "..."}` (sent by the device through a new USB HID keyboard next to the consumer-control media keys, works without the host) or `{"macro": "<id>"}` (run by the host: SendInput keys, Unicode text, waits, open). `macros` list in the config (host-only steps; the device keeps the ids). Same key-name table on both sides (`actions::parseKeys`, `KeyCombo`), shared test vectors. No hold-to-press mode (decided with the user). ACTION gains `id` (additive in v1). Console: `keys <shortcut>`, `macro <id>`.
- 2026-11-08: Editor files are served with `Cache-Control: no-cache`, so a host update never runs with the previous version's scripts.
- 2026-11-08: Phase 11 screen power: pure policy in `firmware/lib/screen_logic` (native tests), applied by `src/display/screen.cpp` with 300 ms fades. Night window from the synced clock (unknown time = day). The brightness knob changes the current period's level (day or night) until the next config. Dark when the PC sleeps (USB suspend, if `sleep_on_pc_sleep`), after `display.toggle`, or at night brightness 0; the first input only wakes it, and a screen woken while it should be dark stays on 30 s after the last input (night level 0 wakes at 10 %). While the backlight is off, LVGL does not render (`lvgl_port::setSleeping`); on wake one full frame is drawn before the fade-in.
- 2026-11-08: Packaging: the web editor is embedded in the exe (`EmbeddedFileProvider`, resource names `RezPad.Host.web.<path with dots>`) instead of copied next to it; the host publishes as one self-contained win-x64 file (`tools/package.ps1`), symbols embedded. Installer is a per-user PowerShell script (`setup/install.ps1`, no admin except the optional startup task through the existing `--register-startup` helper); uninstall leaves a startup task that points to another copy (e.g. a dev build). An unelevated `rez-pad.exe --exit` closes an elevated instance (checked). ESP Web Tools page not built (optional); the zip carries the factory image and esptool instructions.
- 2026-11-08: Open source under MIT (`LICENSE`, copyright rez-lab), bundled components listed in `THIRD-PARTY-NOTICES.md` (keep it current when adding a library or NuGet package); both ship in the release zip. Exe version info names rez-lab as company. README rewritten for people building their own.
- 2026-11-08: Phase 12 spectrum: host `Audio/LoopbackCapture.cs` (WASAPI shared loopback on the default render device, polled every 10 ms, follows default device changes) and `Audio/SpectrumBridge.cs` (capture only while connected and the saved layout has a `spectrum` widget; pauses after 1 s of silence); analysis in `RezPad.Protocol/Spectrum.cs` (2048-point FFT, Hann, 32 log bands 50 Hz to 16 kHz, -70 to 0 dBFS, +3 dB/octave tilt, instant rise and 0.6 s fall), unit-tested. Device: `src/audio/spectrum.*` (falls by itself 300 ms after the last frame), `widgets/spectrum_widget.*` (one object drawing all bars in LV_EVENT_DRAW_MAIN, marking only the strips between old and new bar tops, merged into at most 8 dirty areas: one LVGL object per bar overflowed LVGL's 32 dirty areas and redrew the full screen, 5 FPS; now 29 FPS at 11 % SPI; hidden pages skip spectrum updates). `GET /api/spectrum` feeds the editor preview. SPECTRUM body as already reserved in protocol v1.
- 2026-11-08: Host serial reads no longer wait (COMMTIMEOUTS return immediately, the read thread sleeps 1 ms when idle): a waiting read on the synchronous handle held every write back about 31 ms. Since such reads do not fail when the device resets, a failed write ends the session (plus a COM port presence check every 250 ms while idle).
- 2026-11-08: Phase 12 per-app volume = the Now Playing app: actions `media.app_volume` (rotate) and `media.app_mute` (press), volume widget `target` `master`/`app`, VOLUME gains optional `app {name, level, muted}` (additive in v1). Host `Audio/AppVolume.cs` enumerates Core Audio sessions on the default device each call and changes every session whose process matches the GSMTC app id (`RezPad.Protocol/AppIds.cs`: package AUMID, exe name, Firefox's hashed id), polled every 1 s for changes made in the mixer. Offline the app actions do nothing (no HID key for it). An app picked by name was left out (rotate gestures take action names only; can be added later as an object binding).
- 2026-11-08: Phase 12 mixer widget (what the user meant by per-app volume): new host message MIXER 0x16 (additive in v1) with up to 8 apps grouped by process like the Windows mixer (`Audio/MixerService.cs`, sorted by name, System sounds last, the host's own loopback session left out, `active` from the session's peak meter), sent on change (checked every 1 s) by `Audio/MixerBridge.cs`; ACTION `mixer.volume` / `mixer.mute` carry the app in `id`. Device: `src/audio/mixer.*` owns the list and the focus; action `mixer.focus` (press) toggles it; while focused knob 1 turn selects, knob 2 turn sets the volume (optimistic), knob 2 click mutes, knob 1 click unfocuses, other gestures keep their mapping; unfocus after 15 s idle, page change or host loss. Focus ring is an outline (a wider border would shift the card's children). Console `mixer` and `input <enc> <gesture> [N]` (injects a knob gesture through the full input path, for testing).
- 2026-11-08: Animated images keep every frame: the host thins frames only to fit the 1 MB file limit (`AssetFormat.Thin`, exact count, even spread, same duration; halving to a 2 MB decoded budget had turned a 120-frame GIF into 15 frames at 1.4 FPS). The device pre-decodes when it fits (2 MB per widget, 4 MB in all) and otherwise decodes each JPEG frame when it is due into one of two buffers.
- 2026-11-08: Mixer widget follow-ups: `layout` `list`/`faders`; stronger selection (accent background and edge, other apps dimmed); status bar hints (knob roles while focused, the focus gesture or "nothing mapped" for 5 s after a page with a mixer is shown); editor setup card that maps a free gesture to `mixer.focus` in one click.
- 2026-11-08: Status bar: at most 5 page dots in a sliding window (smaller edge dots when more pages exist), page name cut at 140 px, and `status_bar.items` (up to 3: `date`, `room_temp`, `room_hum`, `outdoor_temp`, `volume`, `metric` with `source`, optional short `label`), global for all pages. Schema 1 is unreleased, so no migration. Metric items count toward the 16 sources and are sent by the host like widget sources.
- 2026-11-08: Editor: size chips also offer sizes that grow left or up (widgets at the right or bottom edge); the screen preview scales to the stage width and the window height (up to 3x) for big monitors; the Grid, Colors and Knobs popovers became an inline "page settings" section under the screen (the head buttons jump to it), with a card per knob or button showing every gesture's action on this page.
- 2026-11-09: Mixer priority list: widget `apps` (ids, up to 8, stored comma-joined in `WidgetSpec::apps[64]`) and `only`; each mixer widget shows `mixer::view()` (its apps first, then the rest unless `only`), selection is a position in the focused widget's view and starts at its top; the host puts all widgets' pinned apps first before the 8-app cut. Fader names are two fixed-height lines (DOTS only at the end), columns 60 px, faders from 100 px high; the preview mirrors it.
- 2026-11-09: Phase 13 integrations start with Beszel. Settings live in `%APPDATA%\rez-pad\integrations.json` (host-only, password encrypted with DPAPI for the current user; the config pushed to the device holds only source ids). `Integrations/BeszelService.cs` signs in as a read-only hub user (PocketBase `auth-with-password`, re-signs on 401), reads `systems` every `refresh_s` (default 15 s) and each up system's newest `system_stats` 1m record once a minute; `RezPad.Protocol/BeszelMap.cs` maps them (checked against hub 0.21.1 with 0.17/0.18/0.21 agents: info cpu/mp/dp/la/dt/u/bb (b in MB/s on old agents)/efs, stats b [sent, received] bytes/s, mu/du, t sensors). Sources `bz.<slug>.<metric>` (slug a-z0-9- up to 14, so ids fit the device's 31 bytes) appear in `/api/sources` and `/api/catalog` with a `group` ("Beszel: <machine>") and go out in SYS_STATS like PC sources. `GET /api/integrations/beszel/raw` shows the hub's records for debugging.
- 2026-11-09: Data source limit raised from 16 to 32 (device `kMaxStats`, both validators).
- 2026-11-09: `server` widget (min 110 x 60): one Beszel machine by `system` slug; uses the 8 sources in `cfg::kServerMetrics` / `ConfigCatalog.ServerMetrics` (online, cpu_load, mem_used_pct, disk_used_pct, load1, temp, net_rx_kbps, net_tx_kbps), which count toward the limit and are sent by the host like widget sources.
- 2026-11-09: Data sources are picked with a step-by-step browser (`web/sources.js`): place (This PC, room sensor, each Beszel machine with its online dot; skipped when there is only one), category (fixed order: CPU, GPU, Memory, Network, Disks, Fans / Usage, Disks, Load, Network, Temperature, Status), then value tiles with live values; it opens at the current value's category, the path leads back up, and search lists matches from every place with their path. Replaces the single long list (and the plain dropdown of status bar items).
- 2026-11-09: Settings view redesigned: category column (Look / Controls / Data, `CATEGORIES` in `web/settings.js`, last choice in localStorage) and one category per screen with its own header; a card titled like its category gives its heading to the header. Shared control styles: switches for checkboxes, own dropdown chevron, 36 px fields with a focus ring, outlined secondary buttons next to primary ones; fields pack to the top so side-by-side fields line up.
- 2026-11-09: Versioning (docs/versioning.md): SemVer, one version for the whole release in `VERSION` (read by `host/Directory.Build.props`, `firmware/scripts/version.py` which generates the git-ignored `src/version_info.cpp` with version + commit + `.dirty` and the USB bcdDevice, and `tools/package.ps1`, which warns on an untagged or dirty build). 0.x until the first public release. 0.1.0 = Phase 11 (tag on c5420a1), 0.2.0 = Phase 12 plus Beszel and the editor redesigns. `CHANGELOG.md` per release.
- 2026-11-08: App icon is original artwork from `tools/gen_app_icon.py`; the tray shows a grey-screen variant while the device is not connected.
- 2026-11-09: Data source limit raised from 32 to 128. It was never a chip limit: the device keeps the values in a fixed array (`kMaxStats`, about 40 bytes each, now about 5 KB of internal RAM) and one SYS_STATS packet must hold them all (128 ids of 31 bytes still fit the 8 KB packet). The SYS_STATS parse buffer moved off the link task stack.
- 2026-11-09: Knob and button numbering: `enc1..enc3` / `btn1..btn4` are numbers; `inputs.knob_ports` / `inputs.button_ports` map each number to a physical port (0 = not used), validated on both sides with shared vectors. Additive within schema 1: without them the old meaning holds (knobs 1 and 2 on ports 1 and 2, `buttons: n` = button ports 1 to n). Knob port 3 on GPIO 39/40/41 (the dropped SD card's pins; decided with the user, not yet verified on the board). The input module samples every port and turns port events into numbered events. "Assign" in Settings: CMD `input_learn` + INPUT_LEARNED 0x92 (additive in v1), `POST /api/device/learn`; the learned control is consumed and inputs stay quiet 0.8 s. Gestures stay with the number (decided with the user); a swap moves `counts_per_detent` / `invert` with the physical knob.
- 2026-11-09: Firmware update from the web page through the ROM loader on the native USB port (decided with the user over an OTA partition scheme): CMD `bootloader` restarts into download mode (`usb_persist_restart(RESTART_BOOTLOADER)`, the board becomes 303A:1001), then `Device/FirmwareUpdater.cs` speaks the ROM protocol itself (SLIP, no stub, 1 KB blocks, MD5 check; pure parts in `RezPad.Protocol/EspLoader.cs` with tests), clears the forced download boot and resets with RTS. About 36 s for the 1.7 MB image; LittleFS is untouched. Also installs onto a board held in download mode. The host embeds `firmware.factory.bin` (`tools/package.ps1` builds the firmware first); the firmware carries `rez-pad-fw-version:<version>` so images show their version. esptool and ESP Web Tools keep working.
- 2026-11-09: Editor polish: app bar with the logo (screen lit while connected), icon tabs, a device pill (state, update available, update progress), undo and redo icons; page name as a title with a pencil (in-place rename); a remove button on the selected widget; new metric widgets start without a source and open the source browser (server cards without a machine); the room sensor is a category of This PC, so without Beszel the browser starts at the categories. `REZPAD_WEB_DIR` makes the host serve `web/` from disk for development.
