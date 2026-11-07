# rez-pad: USB Desk Companion (ESP32-S3 + Windows Host)

This file is the project overview and the rules for working on it. Read it fully before doing any work. Detailed specs live in `docs/`:

- `docs/hardware.md`: inventory, pin map, power, open hardware checks
- `docs/protocol.md`: USB protocol v1 (+ `docs/protocol-test-vectors.json`)
- `docs/config-schema.md`: config file, widget catalog, sources, actions, web editor, host API
- `docs/measurements.md`: real measurements from the hardware

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

ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM, native USB + USB-UART bridge), 3.5" ILI9488 SPI TFT (480x320 landscape, no touch controller), AHT20 + BMP280 on I2C, 2x KY-040 encoders, 0 to 2 optional buttons.

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
  docs/
    hardware.md        pin map, wiring, power, photos
    protocol.md        protocol v1 (moved from this file)
    config-schema.md   config schema and widget catalog
    measurements.md    SPI speed tests, timings, sensor calibration
    protocol-test-vectors.json  shared by C# and C++ protocol tests
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
    test/              PlatformIO native tests (protocol, config validation)
  tools/
    gen_protocol_vectors.py   reference framing implementation, generates shared test vectors
  host/
    RezPad.slnx
    src/RezPad.Host/
    src/RezPad.Protocol/
    tests/RezPad.Protocol.Tests/
  web/
    index.html
    app.js
    styles.css
    vendor/
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

**Stretch**
- `spectrum` widget (WASAPI loopback capture + FFT on host, 16 to 32 bins at ~30 Hz).
- SD card on SPI3 for an art cache or large icon/font packs.
- Optional device Wi-Fi mode for standalone weather.
- Per-app volume control.

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
- 2026-11-07: The CDC port keeps Arduino's reboot-to-bootloader triggers (esptool sequence, 1200 baud), so the native port can flash too; the host must not open it at 1200 baud.
