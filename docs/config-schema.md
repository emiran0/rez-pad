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
  "weather": { "lat": 0.0, "lon": 0.0, "name": "Example City", "refresh_min": 15 },
  "sensor": { "temp_offset_c": -1.5, "hum_offset_pct": 0, "trend_threshold_hpa": 1.0 },
  "media": { "app_priority": ["Spotify", "*"], "art_size": 200, "volume_step": 2 },
  "stats": { "rate_hz": 1 },
  "inputs": {
    "buttons": 0,
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

`inputs.buttons` (0 to 2) is the number of physical buttons fitted. `btn1`/`btn2` mappings are kept but ignored when the button is not fitted. Default is 0 until the hardware is decided.

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

Stable dotted IDs. Examples: `sys.cpu.load`, `sys.cpu.temp`, `sys.cpu.clock_mhz`, `sys.ram.used_pct`, `sys.ram.used_gb`, `sys.gpu.load`, `sys.gpu.temp`, `sys.gpu.vram_pct`, `sys.disk.C.used_pct`, `sys.net.down_kbps`, `sys.net.up_kbps`, `sys.fan.<n>.rpm`, `room.temp`, `room.hum`, `room.pressure`. The host publishes the full catalog of what exists on this PC; the web editor only offers sources from that catalog.

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
