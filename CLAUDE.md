# DeskHub: USB Desk Companion (ESP32-S3 + Windows Host)

This file is the main project brief. Read it fully before doing any work. It is the single source of truth for hardware, architecture, protocol and roadmap until the content is split into `docs/` (see Phase 0).

"DeskHub" is a working name. It can be renamed later.

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

## 2. Hardware inventory

| Part | Notes |
|---|---|
| **ESP32-S3 N16R8** dev board, two USB-C ports | 16 MB flash, 8 MB octal PSRAM. One USB-C is native USB (GPIO19/20), the other goes through a USB-UART bridge. **Main MCU.** |
| **3.5" SPI TFT, ILI9488, 320x480**, with SD slot | **Main display.** Used in landscape (480x320). Has touch pins but no touch controller: leave all `T_*` pins unconnected. |
| **AHT20 + BMP280** combo module | I2C. AHT20 = temp/humidity (0x38). BMP280 = pressure/temp (0x76 or 0x77, detect at runtime). |
| **2x rotary encoder modules** (KY-040 style) | Pins: CLK, DT, SW, +, GND. Clickable knob. |
| Raspberry Pi Pico 2 W | Backup MCU. Not used in the main build. |
| 2.0" ST7789V 240x320 TFT | Bench/test display, or a future "mini" variant. Not used in the main build. |
| Raspberry Pi Zero 2 W | Not used. |
| Optional: 1 to 2 tactile buttons | Not yet purchased. Firmware must work with zero, one or two buttons (configurable). |

### Hardware facts that drive the design
- **ILI9488 over SPI only accepts 18-bit color (RGB666, 3 bytes/pixel).** A full 480x320 frame is ~460 KB.
  - 40 MHz SPI: ~90 ms per full-screen redraw.
  - 80 MHz SPI: ~45 ms (only if the module is stable at that speed; must be measured).
  - Therefore: no full-screen sliding animations. Page switches are instant redraws. Everything else is partial updates of small regions.
- **ILI9488 modules often do not tri-state MISO (SDO).** Never connect the display's SDO/MISO. If the SD card is used, it goes on a separate SPI bus.
- **KY-040 modules have pull-up resistors to the `+` pin.** Power them from **3.3 V only**, never 5 V.
- **Sensor self-heating:** the sensor must be mounted away from the ESP32-S3 and the display backlight (short cable or vented corner). A user-configurable calibration offset is required.

---

## 3. Pin map (ESP32-S3 N16R8)

> **Status: proposed. Must be verified against the actual board silkscreen and module labels before wiring.** If any pin conflicts with the actual board (for example the onboard RGB LED), update `firmware/include/pins.h` AND this table together.

### Pins that must NOT be used
- GPIO26 to GPIO37: flash and octal PSRAM.
- GPIO19, GPIO20: native USB D-/D+.
- GPIO43, GPIO44: UART0 to the USB-UART bridge (debug logs).
- GPIO0, GPIO3, GPIO45, GPIO46: strapping pins.
- GPIO38 or GPIO48: onboard RGB LED on most DevKitC-1 style boards (depends on board revision). Treat both as reserved until confirmed.

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

### Power
- Encoders `+`: **3.3 V**.
- Sensor VCC: 3.3 V.
- Display VCC: many 3.5" ILI9488 modules have an onboard 3.3 V regulator and accept 5 V on VCC (some have a `J1` jumper to bypass it for 3.3 V). **Check the module.** Logic levels are 3.3 V in all cases.
- The final device is powered from the **native USB** port.

---

## 4. System architecture

```
+---------------------------- Windows PC -----------------------------+
|                                                                     |
|  DeskHub Host (C# / .NET, tray app, runs elevated at logon)         |
|   - MediaService     (GSMTC: now playing, art, controls, seek)      |
|   - AudioService     (Core Audio: master volume, mute)              |
|   - SystemStats      (perf counters + LibreHardwareMonitorLib)      |
|   - WeatherService   (Open-Meteo, no API key)                       |
|   - ConfigService    (master config JSON in %APPDATA%\DeskHub)      |
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
- PlatformIO, Arduino framework on top of ESP-IDF (Arduino-ESP32 core 3.x). Use whichever PlatformIO platform currently ships core 3.x for ESP32-S3 (verify; the community `pioarduino` platform has been the usual route). Pure ESP-IDF is acceptable if Arduino blocks something important; discuss before switching.
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
- Board: `esp32-s3-devkitc-1` with overrides for N16R8: 16 MB flash, `qio_opi` memory type, PSRAM enabled (`BOARD_HAS_PSRAM`).
- USB mode: **USB-OTG (TinyUSB)**, so the native port can be CDC + HID.
- `Serial` = UART0 (the bridge USB-C port) for **debug logs only**. The protocol runs on the native USB CDC.
- Custom USB descriptors: manufacturer "DeskHub", product "DeskHub", a stable serial string. Host discovers the port by VID/PID and product string.
- Custom partition table: single large app partition (no OTA needed initially) plus a large LittleFS partition for config, fonts and icons.
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
- **C# on the current .NET LTS** (verify version), target framework `netX.0-windows10.0.19041.0` or newer for WinRT APIs.
- Tray app (WinForms `NotifyIcon` is enough) plus **ASP.NET Core Kestrel** bound to `127.0.0.1:47800` (port configurable).
- `System.IO.Ports` for serial.
- `Windows.Media.Control` (GSMTC) for media.
- Core Audio for volume (NAudio or direct COM interop).
- **LibreHardwareMonitorLib** for temps, fans, clocks, GPU. Check the current release notes for its kernel driver requirements (recent builds moved from the WinRing0 driver to PawnIO). Detect missing driver/admin and show guidance in the tray menu and web UI.
- `Windows.Graphics.Imaging` (built-in WinRT) for decode/resize/JPEG encode of album art. Avoid image libraries with restrictive licenses.

**Verify current stable versions of all NuGet packages before adding them.**

### Solution layout
```
host/
  DeskHub.sln
  src/DeskHub.Host/          tray app, services, Kestrel, web assets
  src/DeskHub.Protocol/      COBS, CRC, message types (no Windows deps, unit-testable)
  tests/DeskHub.Protocol.Tests/
```

### Services
- **SerialLink**: find the device by USB VID/PID and product string (WMI or SetupAPI), open, reconnect loop with backoff, heartbeat. Handles unplug/replug within 3 s.
- **MediaService**: `GlobalSystemMediaTransportControlsSessionManager`. Subscribe to `CurrentSessionChanged`, `MediaPropertiesChanged`, `PlaybackInfoChanged`, `TimelinePropertiesChanged`. Optional app priority list in config (for example prefer Spotify over browser tabs). Executes play/pause, next, prev, seek (`TryChangePlaybackPositionAsync`, may be unsupported by some apps; report `can_seek`).
- **ArtProcessor**: open thumbnail stream, center-crop to square, resize to the size requested by the active layout (default 200x200), encode JPEG (quality ~85). Cache last N by `track_id`. Skip resending art the device already has.
- **AudioService**: master endpoint volume and mute, event-driven change notifications, executes relative volume actions with configurable step.
- **SystemStatsService**: CPU load, RAM, disk usage, network throughput via performance counters/PDH. Temps, fans, clocks, GPU via LibreHardwareMonitorLib. Exposes a **catalog of available data sources** (stable IDs plus display names) to the web UI. Sends only the sources referenced by the current config, at the configured rate (default 1 Hz, max 4 Hz).
- **WeatherService**: Open-Meteo forecast API (no key) plus Open-Meteo geocoding for the city search in the web UI. Refresh every 15 min. Metric/imperial from config.
- **ConfigService**: `%APPDATA%\DeskHub\config.json`, JSON schema validation, versioned, pushes to device on change or CRC mismatch.
- **WebServer**: serves `web/` static files plus a REST/WebSocket API (see section 8).

### Startup and elevation
- Temps need admin. GSMTC only works inside the user session, so the host **cannot be a Windows service**.
- Solution: register a **Task Scheduler task at user logon with "Run with highest privileges"**. Provide a tray menu item "Start with Windows" that creates/removes this task.
- Without admin the app still runs; temps/fans show as unavailable.

### Resource targets
- Average CPU < 1%, memory < 150 MB.

---

## 7. USB protocol (v1)

### Framing
- Transport: USB CDC (baud setting is ignored by native USB).
- Each frame on the wire: `COBS(packet) + 0x00`.
- `packet` = `[ver u8 = 1][type u8][seq u16 LE][body ...][crc16 u16 LE]`
- CRC16-CCITT (0x1021, init 0xFFFF) over `ver..body`.
- Max decoded packet size: 8192 bytes. Larger data (art, config) is chunked.
- Body is either UTF-8 JSON (small structured messages) or a defined binary layout (bulk data). Each type below says which.
- Any breaking change bumps `PROTOCOL_VERSION`. Both sides report their version in the handshake and refuse incompatible peers with a clear message.

### Host -> Device

| Type | Name | Body | Notes |
|---|---|---|---|
| 0x01 | HELLO | JSON `{proto, host_ver}` | On connect |
| 0x02 | PING | empty | Every 1 s |
| 0x03 | TIME_SYNC | JSON `{unix_ms, tz_offset_min}` | On connect, then every 60 s |
| 0x10 | NOW_PLAYING | JSON, see below | On change, plus resync every 5 s |
| 0x11 | ART_BEGIN | JSON `{id, w, h, fmt:"jpeg", size, crc32}` | New `id` aborts any transfer in progress |
| 0x12 | ART_CHUNK | binary `[id u32][offset u32][data]` | |
| 0x13 | ART_END | JSON `{id}` | Device verifies CRC32, decodes, swaps image |
| 0x14 | ART_NONE | JSON `{id}` | Track has no art, show placeholder |
| 0x15 | VOLUME | JSON `{level, muted}` | level 0 to 100 |
| 0x20 | SYS_STATS | JSON `{t, v:{"<source_id>": number, ...}}` | Only referenced sources |
| 0x30 | WEATHER | JSON, see below | Every 15 min and on connect |
| 0x40 | CONFIG_BEGIN | JSON `{size, crc32}` | |
| 0x41 | CONFIG_CHUNK | binary `[offset u32][data]` | |
| 0x42 | CONFIG_END | empty | Device validates, saves, applies, replies CONFIG_ACK |
| 0x43 | CONFIG_GET | empty | Device replies with its config (chunked, same scheme reversed) |
| 0x50 | SPECTRUM | binary `[n u8][bins u8 * n]` | Stretch goal, ~30 Hz |
| 0x60 | CMD | JSON `{cmd, args}` | Debug/maintenance: `set_spi_hz`, `reboot`, `factory_reset`, `bench_display`, `identify` |

`NOW_PLAYING` body:
```json
{
  "state": "playing",
  "track_id": 2849123344,
  "title": "Song Title",
  "artist": "Artist",
  "album": "Album",
  "app": "Spotify",
  "pos_ms": 81234,
  "dur_ms": 215000,
  "ts": 1793912345678,
  "can_seek": true,
  "can_next": true,
  "can_prev": true
}
```
`state` is one of `playing`, `paused`, `stopped`, `none`. `track_id` is a 32-bit hash of title + artist + album. `ts` is the host unix time in ms when `pos_ms` was sampled.

`WEATHER` body:
```json
{
  "loc": "Example City",
  "units": "metric",
  "updated": 1793912345,
  "now": { "temp": 14.2, "feels": 12.9, "hum": 71, "wind": 12.0, "code": 3, "is_day": false },
  "hourly": [ { "t": 1793913600, "temp": 13.8, "code": 3, "pop": 10 } ],
  "daily":  [ { "d": "2026-11-06", "min": 9.1, "max": 16.4, "code": 61, "pop": 60 } ]
}
```
`code` is the WMO weather code from Open-Meteo. Send 12 hourly and 5 daily entries.

### Device -> Host

| Type | Name | Body | Notes |
|---|---|---|---|
| 0x81 | HELLO_ACK | JSON `{proto, fw_ver, w, h, psram_free, cfg_crc, buttons}` | Reply to HELLO |
| 0x82 | PONG | empty | Reply to PING |
| 0x90 | ACTION | JSON `{action, value}` | Resolved from input map, see section 8 |
| 0x91 | ART_REQUEST | JSON `{id}` | Device lost or never received art for current track |
| 0xA0 | SENSOR | JSON `{temp_c, hum_pct, press_hpa, trend}` | Every 10 s |
| 0xC0 | CONFIG_ACK | JSON `{ok, crc32, error}` | |
| 0xC1..0xC3 | CONFIG_DATA_BEGIN / CHUNK / END | | Reply to CONFIG_GET |
| 0xE0 | LOG | JSON `{lvl, msg}` | Mirrors important device logs to the host |
| 0xE1 | ERROR | JSON `{code, msg}` | |
| 0xE2 | CMD_RESULT | JSON `{cmd, ok, data}` | |

### Connection rules
- Host considers the device lost after 3 s without PONG. Device considers the host lost after 3 s without PING.
- On (re)connect: HELLO -> HELLO_ACK -> TIME_SYNC -> config check (push if CRC differs) -> NOW_PLAYING -> art -> VOLUME -> WEATHER -> SYS_STATS stream.

---

## 8. Configuration

### Config file (host master copy, device mirror)
```json
{
  "schema": 1,
  "display": {
    "brightness": 80,
    "night": { "enabled": true, "from": "23:00", "to": "07:30", "brightness": 15 },
    "sleep_on_pc_sleep": true,
    "spi_hz": 40000000
  },
  "grid": { "cols": 4, "rows": 3 },
  "theme": { "name": "dark", "accent": "#4FC3F7" },
  "units": { "temp": "c", "pressure": "hpa", "wind": "kmh" },
  "weather": { "lat": 0.0, "lon": 0.0, "name": "Example City", "refresh_min": 15 },
  "sensor": { "temp_offset_c": -1.5, "hum_offset_pct": 0, "trend_threshold_hpa": 1.0 },
  "media": { "app_priority": ["Spotify", "*"], "art_size": 200, "volume_step": 2 },
  "stats": { "rate_hz": 1 },
  "inputs": {
    "enc1": { "counts_per_detent": 4, "invert": false,
              "rotate": "nav.page", "click": "nav.home", "double": "none", "long": "display.toggle" },
    "enc2": { "counts_per_detent": 4, "invert": false,
              "rotate": "media.volume", "click": "media.play_pause", "double": "media.next", "long": "media.mute" },
    "btn1": { "click": "media.prev", "long": "none" },
    "btn2": { "click": "media.next", "long": "none" }
  },
  "pages": [
    {
      "id": "now_playing",
      "name": "Now Playing",
      "inputs_override": { "enc1": { "rotate": "media.seek" } },
      "widgets": [
        { "type": "album_art",      "x": 0, "y": 0, "w": 2, "h": 2 },
        { "type": "track_info",     "x": 2, "y": 0, "w": 2, "h": 2 },
        { "type": "media_controls", "x": 0, "y": 2, "w": 3, "h": 1 },
        { "type": "volume",         "x": 3, "y": 2, "w": 1, "h": 1 }
      ]
    },
    {
      "id": "system",
      "name": "System",
      "widgets": [
        { "type": "metric_gauge", "x": 0, "y": 0, "w": 1, "h": 1, "source": "sys.cpu.load",  "label": "CPU" },
        { "type": "metric_gauge", "x": 1, "y": 0, "w": 1, "h": 1, "source": "sys.cpu.temp",  "label": "CPU C" },
        { "type": "metric_gauge", "x": 2, "y": 0, "w": 1, "h": 1, "source": "sys.gpu.load",  "label": "GPU" },
        { "type": "metric_gauge", "x": 3, "y": 0, "w": 1, "h": 1, "source": "sys.gpu.temp",  "label": "GPU C" },
        { "type": "metric_graph", "x": 0, "y": 1, "w": 2, "h": 1, "source": "sys.cpu.load",  "label": "CPU history" },
        { "type": "metric_bar",   "x": 2, "y": 1, "w": 2, "h": 1, "source": "sys.ram.used_pct", "label": "RAM" },
        { "type": "metric_value", "x": 0, "y": 2, "w": 2, "h": 1, "source": "sys.net.down_kbps", "label": "Down" },
        { "type": "metric_value", "x": 2, "y": 2, "w": 2, "h": 1, "source": "sys.net.up_kbps",   "label": "Up" }
      ]
    },
    {
      "id": "weather_room",
      "name": "Weather & Room",
      "widgets": [
        { "type": "weather_now",      "x": 0, "y": 0, "w": 2, "h": 2 },
        { "type": "room_climate",     "x": 2, "y": 0, "w": 2, "h": 2 },
        { "type": "weather_forecast", "x": 0, "y": 2, "w": 4, "h": 1, "mode": "daily" }
      ]
    }
  ]
}
```

### Widget catalog (firmware implements, web editor offers)

| Type | Allowed sizes (w x h) | Data |
|---|---|---|
| `album_art` | 2x2 | media art |
| `track_info` | 2x1, 2x2, 4x1 | title, artist, album, app; long text scrolls (marquee) |
| `media_controls` | 2x1, 3x1, 4x1 | prev/play/next state, progress bar, elapsed/duration |
| `progress` | 2x1, 4x1 | progress bar only |
| `volume` | 1x1, 2x1 | volume level, mute |
| `metric_value` | 1x1, 2x1 | any numeric source, label, unit |
| `metric_bar` | 2x1, 4x1 | any numeric source with min/max |
| `metric_gauge` | 1x1, 2x2 | arc gauge, min/max, warning/critical thresholds |
| `metric_graph` | 2x1, 2x2, 4x1 | sparkline history (device keeps a ring buffer per source) |
| `clock` | 1x1, 2x1 | time and date, 12/24 h |
| `room_climate` | 2x1, 2x2 | temp, humidity, pressure, trend arrow |
| `weather_now` | 2x1, 2x2 | icon, temp, feels like, humidity, wind |
| `weather_forecast` | 4x1 | `hourly` or `daily` |
| `label` | any | static text |
| `spectrum` | 4x1 | stretch goal |

Widgets may have optional style fields (`label`, `unit`, `min`, `max`, `warn`, `crit`, `decimals`). Validation must reject overlapping widgets, out-of-grid widgets and unsupported sizes, on both host and device.

### Data source IDs
Stable dotted IDs. Examples: `sys.cpu.load`, `sys.cpu.temp`, `sys.cpu.clock_mhz`, `sys.ram.used_pct`, `sys.ram.used_gb`, `sys.gpu.load`, `sys.gpu.temp`, `sys.gpu.vram_pct`, `sys.disk.C.used_pct`, `sys.net.down_kbps`, `sys.net.up_kbps`, `sys.fan.<n>.rpm`, `room.temp`, `room.hum`, `room.pressure`. The host publishes the full catalog of what exists on this PC; the web editor only offers sources from that catalog.

### Actions
`none`, `nav.page` (rotate), `nav.next_page`, `nav.prev_page`, `nav.home`, `media.play_pause`, `media.next`, `media.prev`, `media.seek` (rotate, seconds per detent configurable), `media.volume` (rotate), `media.mute`, `display.toggle`, `display.brightness` (rotate).

### Web config editor
- Location: `web/` in the repo, served by the host.
- Plain HTML/CSS/JS (no build step required) with **gridstack.js** (verify license and version) for the grid.
- Features: page list (add, remove, rename, reorder), grid editor with a widget palette restricted to allowed sizes, properties panel per widget (source dropdown from the host's catalog, labels, thresholds), input mapping form, display/night settings, weather location search (Open-Meteo geocoding), units, sensor offsets with live readings, device status (connected, firmware version, protocol version).
- A 480x320 scaled preview that approximates the device look.
- Save -> host validates -> writes master config -> pushes to the device -> shows result from CONFIG_ACK.

### Host API (localhost only)
- `GET /api/status`, `GET /api/config`, `PUT /api/config`, `GET /api/sources`, `GET /api/geocode?q=`, `GET /api/sensor`, `POST /api/device/cmd`, WebSocket `/ws` for live status and sensor updates.
- Bind to `127.0.0.1` only. No remote access.

---

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
deskhub/
  CLAUDE.md
  docs/
    hardware.md        pin map, wiring, power, photos
    protocol.md        protocol v1 (moved from this file)
    config-schema.md   config schema and widget catalog
    measurements.md    SPI speed tests, timings, sensor calibration
  firmware/
    platformio.ini
    partitions.csv
    include/pins.h     single source of truth for pins (mirrors docs/hardware.md)
    src/
      main.cpp
      link/            USB CDC, COBS, CRC, dispatch
      hid/             consumer control fallback
      display/         LovyanGFX setup, backlight
      ui/              LVGL setup, status bar, page manager
      widgets/         one file per widget type
      input/           encoders, buttons, gestures, action mapping
      sensors/         AHT20, BMP280, filtering, trend
      config/          load, validate, save, defaults
      fonts/           generated LVGL fonts
    test/              PlatformIO native tests (protocol, config validation)
  host/
    DeskHub.sln
    src/DeskHub.Host/
    src/DeskHub.Protocol/
    tests/DeskHub.Protocol.Tests/
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
- `DeskHub.Protocol` with COBS, CRC16, CRC32, message types, unit tests.
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

## 13. Open questions (ask the user when relevant)

- Exact ESP32-S3 board model/revision: which USB-C port is native USB, and which GPIO is the onboard RGB LED (38 or 48)?
- 3.5" module: does VCC go through an onboard regulator (is there a J1 jumper)? Does the LED pin have a transistor or need an external driver?
- Encoder detent count (2 or 4 pulses per click) and preferred rotation direction.
- Will physical buttons be added, and how many?
- Enclosure plans (affects sensor placement and display orientation).
