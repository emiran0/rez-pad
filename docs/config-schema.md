# Configuration (schema 1)

The host keeps the master copy at `%APPDATA%\rez-pad\config.json`. The device keeps a mirror in LittleFS (`/config.json`) so it boots into the user's layout without the host. On connect, if the CRC32 of the device copy differs, the host pushes its config.

Schema changes bump `schema` and include a migration on the host.

## The default config

[`default-config.json`](default-config.json) is the built-in default and the reference example. The host embeds that file; `tools/gen_config_headers.py` turns it into `firmware/lib/config_logic/src/default_config.h`, so both sides start from the same bytes. Top-level sections: `schema`, `display`, `grid`, `theme`, `units`, `weather`, `sensor`, `media`, `stats`, `inputs`, `pages`.

## Validation

Implemented twice, tested against the same cases ([`config-test-vectors.json`](config-test-vectors.json)): `host/src/RezPad.Protocol/Config.cs` (`ConfigValidator`) and `firmware/lib/config_logic` (`cfg::parse`). Both report the first problem as `"<path>: <reason>"`, e.g. `pages[0].widgets[1]: overlaps another widget`.

- `schema` must be 1 and `pages` must exist; every other section may be missing (struct defaults). Unknown keys are ignored. A JSON `null` counts as missing.
- Numbers are range-checked (see below), whole numbers where the schema says so; strings have byte limits (UTF-8): page `id` 23, page `name` 31, widget `type` 19, `source` 39, `label` 31, `unit` 11, `text` 63.
- 1 to 8 pages, page ids unique and not empty (`name` defaults to the id), up to 16 widgets per page, at most 16 different data sources over all pages (the device keeps 16 SYS_STATS values).
- Widgets: known type, `x`/`y`/`w`/`h` inside the grid, an allowed size, no overlap. `metric_*` need a `source`, `label` needs `text`, `weather_forecast` `mode` is `daily` (default) or `hourly`, `decimals` 0 to 3, and `min` must be below `max` after source defaults.
- Actions: known names; `nav.page`, `media.seek`, `media.volume`, `display.brightness` take a rotation and are only allowed on `rotate`/`rotate_pressed`; all others only on presses (`click`, `double`, `long`).
- Ranges: `display.brightness` and `night.brightness` 0 to 100, `night.from`/`to` `"HH:MM"`, `spi_hz` 1 to 80 MHz, `grid` 1 to 8, `theme.accent` `"#RRGGBB"`, `sensor.temp_offset_c` -10 to 10, `hum_offset_pct` -20 to 20, `trend_threshold_hpa` 0.1 to 10, `media.volume_step` 1 to 20, `seek_step_s` 1 to 60, `inputs.buttons` 0 to 2, `counts_per_detent` 1 to 8.
- Host only (the device ignores these sections): `weather.lat` -90 to 90 and `lon` -180 to 180 (both or neither), `refresh_min` 5 to 180, `media.art_size` 32 to 480, `media.app_priority` up to 16 names, `stats.rate_hz` 0.2 to 4.

Style fields a metric widget leaves out come from its source: temperatures `°C` 20 to 100 (warn 75, crit 90), loads `%` (80/95), `used_pct`/`vram_pct` `%` (85/95, disks 90/97), other `_pct` `%`, `_kbps` `kB/s` 0 to 10000, `_mhz` `MHz`, `_w` `W`, `_gb` `GB`, `rpm`. `"unit": ""` means no unit.

## Host and device copies

- Host: `Config/ConfigService.cs` loads `%APPDATA%\rez-pad\config.json`, migrates it (a missing section or section key is filled in from the default; files from before Phase 9 only had `weather` and `units`), validates it (an invalid file is kept as `config.json.bad` and the default is used) and writes atomically. The device gets the compact UTF-8 bytes; their CRC32 is the config's identity.
- `Device/ConfigSync.cs` pushes those bytes when HELLO_ACK `cfg_crc` differs and after every saved change, and waits for CONFIG_ACK (docs/protocol.md, "Config transfer").
- Device: `src/config/store.cpp` keeps the file in LittleFS (`/config.json`, written as `/config.tmp`, read back, renamed). At boot it loads and validates it; when it is missing it uses the built-in default, when it is invalid it uses the default and shows a warning in the status bar (the stored CRC stays, so the host pushes a good one). A pushed config is validated and saved on the link task, then the UI task rebuilds every page and applies inputs, sensor offsets, brightness, units, accent color and SPI clock.

Encoder gestures: `rotate`, `rotate_pressed` (turned while the knob is held down; that press then produces no click/double/long), `click`, `double` (when `none`, clicks fire on release without waiting for a second click), `long`. Buttons: `click`, `long`.

Pages may still carry `inputs_override` (same shape as `inputs`, only the keys to change) for page-specific mappings. The default config uses none: an override of `enc1.rotate` would make it impossible to leave that page by turning.

`inputs.buttons` (0 to 2) is the number of physical buttons fitted. `btn1`/`btn2` mappings are kept but ignored when the button is not fitted. Default is 0 until the hardware is decided.

`weather`: no location by default (`lat`/`lon` null): the user picks a city on the web page (Open-Meteo geocoding), and the host fetches nothing until then. `refresh_min` 5 to 180. `units`: `temp` `c`/`f`, `wind` `kmh`/`ms`/`mph`, `pressure` `hpa`/`inhg`; weather values are converted by Open-Meteo (pressure by the host), and the device shows its room sensor in the same units.

`media`: `app_priority` orders media sessions by app (matched case-insensitively against the app name or its AppUserModelId, `*` matches any); a playing session always wins over paused ones, then the list decides. `art_size` is the album art edge in px when the device does not ask for one in HELLO_ACK. `volume_step` is percent per detent, `seek_step_s` seconds per detent.

## Widget catalog (firmware implements, web editor offers)

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
| `label` | any | static `text`, optional small `label` above it |
| `spectrum` | 4x1 | stretch goal, not in the validators yet |

Widgets may have optional style fields (`label`, `unit`, `min`, `max`, `warn`, `crit`, `decimals`); see "Validation" for the rules and defaults.

## Data source IDs

Stable dotted IDs. The host publishes the catalog of what exists on this PC (`GET /api/sources`); the web editor only offers sources from that catalog.

| ID | Unit | From | Notes |
|---|---|---|---|
| `sys.cpu.load` | % | Windows counter "% Processor Utility" | Same number as Task Manager. Falls back to GetSystemTimes |
| `sys.cpu.clock_mhz` | MHz | "Processor Frequency" x "% Processor Performance" | Task Manager's "Speed" |
| `sys.cpu.temp` | °C | LibreHardwareMonitor | Needs the PawnIO driver and admin rights. AMD: Tctl/Tdie, Intel: CPU Package |
| `sys.cpu.power_w` | W | LibreHardwareMonitor | Needs PawnIO and admin |
| `sys.ram.used_pct`, `sys.ram.used_gb`, `sys.ram.total_gb` | %, GB | GlobalMemoryStatusEx | used = total minus available, like Task Manager "In use" |
| `sys.net.down_kbps`, `sys.net.up_kbps` | kB/s (1000 bytes) | NIC byte counters | Connected physical adapters only (no loopback, tunnels, Hyper-V switches). Widgets switch to MB/s from 1000 kB/s |
| `sys.gpu.load` | % | D3DKMT engine statistics (any vendor) | Task Manager's GPU number: the busiest engine node (3D, video decode, copy, ...). Always listed; `--` without a GPU |
| `sys.gpu.3d_load`, `sys.gpu.decode_load`, `sys.gpu.encode_load` | % | D3DKMT | Busiest node of that engine type |
| `sys.gpu.temp`, `sys.gpu.core_load`, `sys.gpu.clock_mhz`, `sys.gpu.power_w`, `sys.gpu.vram_used_gb`, `sys.gpu.vram_pct`, `sys.gpu.fan_pct`, `sys.gpu.fan_rpm` | | NVML on NVIDIA, LibreHardwareMonitor otherwise | `core_load` is the driver's graphics-engine load (ignores video decode). `fan_rpm` needs driver R535+ |
| `sys.gpu.hotspot_temp` | °C | LibreHardwareMonitor | Not in NVML's public API. Its GPU part (vendor DLLs, about 33 ms CPU per refresh) opens only when the layout uses this source |
| `sys.fan.<n>.rpm` | RPM | LibreHardwareMonitor (motherboard SuperIO) | `n` is the header number ("Fan #3" is `sys.fan.3.rpm`); all headers are listed, also at 0 rpm, so ids never shift. Needs PawnIO and admin |
| `sys.disk.<letter>.used_pct`, `sys.disk.<letter>.free_gb` | %, GB | DriveInfo | Fixed drives |
| `room.temp`, `room.hum`, `room.pressure` | | device sensor | Device side, not sent by the host (Phase 8+) |

## Actions

`none`, `nav.page` (rotate), `nav.next_page`, `nav.prev_page`, `nav.home`, `media.play_pause`, `media.next`, `media.prev`, `media.seek` (rotate, seconds per detent configurable), `media.volume` (rotate), `media.mute`, `display.toggle`, `display.brightness` (rotate).

## Web config editor

`web/` in the repo, copied next to the host exe and served at `http://127.0.0.1:47800`. Plain HTML/CSS/JS as ES modules, no build step and no third-party code:

- `app.js`: loading, live data polling, live validation, save and undo.
- `state.js`: the working copy of the config, change events, API helpers.
- `editor.js`: Layout tab. Page tabs (add, rename, reorder, remove), the device screen with draggable widget cards, the widget palette (only types with room for an allowed size), the selected widget's properties (allowed sizes that fit, source dropdown with live values, style fields with the source defaults as placeholders), grid size, and per-page knob overrides.
- `preview.js`: draws the 480x320 screen like the firmware does (same card geometry, colors, fonts sizes and widget layouts, weather icons exported by `tools/gen_weather_icons.py --web web/img/weather`), fed with live data: stats, now playing with album art, weather, room sensor.
- `settings.js`: Knobs, Display (brightness, accent color, SPI clock, night settings for Phase 11), Weather (city search, refresh, units), Sensor & media (offsets with the live reading, app priority, steps, stats rate), Device (status, compare with the device's copy, restart, clear device config, data source table).

Editing changes only the working copy. Every change is validated by the host (`POST /api/config/validate`, same validator as saving); an error blocks saving and "Show" jumps to the widget it names. Save (or Ctrl+S) sends `PUT /api/config?base=<crc>`: the host refuses (409) when the config changed elsewhere since the page loaded, otherwise it saves, pushes to the device and the page shows the device's answer. Live data is polled (status every 2 s, stats and media every 1 s, weather and room every 5 s, paused while the tab is hidden).

## Host API (localhost only)

- `GET /api/status` (includes `config`: host CRC, device CRC, in sync, last push result).
- `GET /api/config` (current config, its CRC in the `X-Config-Crc` header), `PUT /api/config[?base=<crc>]` (validates, saves, pushes; `400 {error}` with the validator's message, `409` when `base` is not the current CRC, otherwise `{saved, crc, device}` where `device` is the push result or null when no device is connected), `POST /api/config/validate` (`{error, bytes}`, saves nothing), `GET /api/config/default`.
- `GET /api/catalog`: widget types with allowed sizes, actions and which of them take a rotation, limits, and every data source on this PC with its style defaults.
- `GET /api/device/config` (the device's stored copy via CONFIG_GET), `POST /api/device/cmd`.
- `GET /api/sources`, `GET /api/media`, `GET /api/media/art?size=` (current album art as JPEG), `GET /api/weather`, `PUT /api/weather`, `GET /api/geocode?q=`, `GET /api/sensor`, `GET /api/hardware`, `GET /api/log`.
- Bind to `127.0.0.1` only. No remote access.
