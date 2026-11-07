# Configuration (schema 1)

The host keeps the master copy at `%APPDATA%\rez-pad\config.json`. The device keeps a mirror in LittleFS (`/config.json`) so it boots into the user's layout without the host. On connect, if the CRC32 of the device copy differs, the host pushes its config.

Schema changes bump `schema` and include a migration on the host.

## Example (the default config)

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
  "weather": { "lat": null, "lon": null, "name": "", "refresh_min": 15 },
  "sensor": { "temp_offset_c": -1.5, "hum_offset_pct": 0, "trend_threshold_hpa": 1.0 },
  "media": { "app_priority": ["Spotify", "*"], "art_size": 200, "volume_step": 2, "seek_step_s": 5 },
  "stats": { "rate_hz": 1 },
  "inputs": {
    "buttons": 0,
    "enc1": { "counts_per_detent": 4, "invert": false,
              "rotate": "nav.page", "rotate_pressed": "media.seek",
              "click": "nav.home", "double": "none", "long": "display.toggle" },
    "enc2": { "counts_per_detent": 4, "invert": false,
              "rotate": "media.volume", "rotate_pressed": "display.brightness",
              "click": "media.play_pause", "double": "media.next", "long": "media.mute" },
    "btn1": { "click": "media.prev", "long": "none" },
    "btn2": { "click": "media.next", "long": "none" }
  },
  "pages": [
    {
      "id": "now_playing",
      "name": "Now Playing",
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

Encoder gestures: `rotate`, `rotate_pressed` (turned while the knob is held down; that press then produces no click/double/long), `click`, `double` (when `none`, clicks fire on release without waiting for a second click), `long`. Buttons: `click`, `long`.

Pages may still carry `inputs_override` (same shape as `inputs`, only the keys to change) for page-specific mappings. The default config uses none: an override of `enc1.rotate` would make it impossible to leave that page by turning.

`inputs.buttons` (0 to 2) is the number of physical buttons fitted. `btn1`/`btn2` mappings are kept but ignored when the button is not fitted. Default is 0 until the hardware is decided.

`weather`: no location by default (`lat`/`lon` null): the user picks a city on the web page (Open-Meteo geocoding), and the host fetches nothing until then. `refresh_min` 5 to 180. `units`: `temp` `c`/`f`, `wind` `kmh`/`ms`/`mph`, `pressure` `hpa`/`inhg`; weather values are converted by Open-Meteo (pressure by the host), and the device shows its room sensor in the same units.

The host reads and writes `config.json` through `Config/ConfigStore.cs`, which so far only owns `weather` and `units` and keeps every other key untouched (the full schema with validation and device push comes in Phase 9).

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
| `label` | any | static text |
| `spectrum` | 4x1 | stretch goal |

Widgets may have optional style fields (`label`, `unit`, `min`, `max`, `warn`, `crit`, `decimals`). Validation must reject overlapping widgets, out-of-grid widgets and unsupported sizes, on both host and device.

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

- Location: `web/` in the repo, served by the host.
- Plain HTML/CSS/JS (no build step) with **gridstack.js** (MIT) vendored in `web/vendor/`.
- Features: page list (add, remove, rename, reorder), grid editor with a widget palette restricted to allowed sizes, properties panel per widget (source dropdown from the host's catalog, labels, thresholds), input mapping form, display/night settings, weather location search (Open-Meteo geocoding), units, sensor offsets with live readings, device status (connected, firmware version, protocol version).
- A 480x320 scaled preview that approximates the device look.
- Save -> host validates -> writes master config -> pushes to the device -> shows result from CONFIG_ACK.

## Host API (localhost only)

- `GET /api/status`, `GET /api/config`, `PUT /api/config`, `GET /api/sources`, `GET /api/geocode?q=`, `GET /api/sensor`, `POST /api/device/cmd`, WebSocket `/ws` for live status and sensor updates.
- Bind to `127.0.0.1` only. No remote access.
