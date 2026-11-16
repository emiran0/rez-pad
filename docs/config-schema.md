# Configuration (schema 1)

The host keeps the master copy at `%APPDATA%\rez-pad\config.json`. The device keeps a mirror in LittleFS (`/config.json`) so it boots into the user's layout without the host. On connect, if the CRC32 of the device copy differs, the host pushes its config.

Schema changes bump `schema` and include a migration on the host.

## The default config

[`default-config.json`](default-config.json) is the built-in default and the reference example. The host embeds that file; `tools/gen_config_headers.py` turns it into `firmware/lib/config_logic/src/default_config.h`, so both sides start from the same bytes. Top-level sections: `schema`, `display`, `grid`, `theme`, `units`, `weather`, `sensor`, `media`, `stats`, `inputs`, `status_bar`, `pages`.

## Screen

`display.panel` is the display module (`ili9488`: 3.5" 320x480, the default; `st7789`: 2.0" ST7789V 240x320) and `display.orientation` how it is mounted (`landscape`, the default, `portrait`, `landscape_flipped`, `portrait_flipped`). Together they give the screen size every widget size is checked against: 480x320, 320x480, 320x240 or 240x320; the grid is the screen below the 20 px status bar. Both keys were added within schema 1: a config without them means the 3.5" display in landscape (the host fills them in from the default).

- The device starts its display from them at boot and keeps the last valid pair in NVS, so a fallback to the built-in default still fits the panel. A pushed config that changes either one is saved and acknowledged (CONFIG_ACK `restart: true`), then the device restarts into it.
- [`default-layouts.json`](default-layouts.json) has the default grid and pages for the other screen shapes (320x480, 240x320, 320x240), keyed `"<w>x<h>"`: the same pages and ids as the default config, rearranged to fit. The host serves them (`GET /api/config/layout?panel=&orientation=`, Settings > Screen > "Use the default layout for this screen"), the firmware falls back to them (`cfg::parseDefault`), and both sides test that every panel and orientation has a valid default.
- Pages made for another shape stay as they are; the ones that do not fit fail validation with the usual widget error, shown in the editor.
- Narrow screens (below 400 px wide): the status bar shows 3 page dots and at most one item, and the page name only where it has 40 px; the knob popup is the screen width less 16 px with a 48 px icon.

## Validation

Implemented twice, tested against the same cases ([`config-test-vectors.json`](config-test-vectors.json)): `host/src/RezPad.Protocol/Config.cs` (`ConfigValidator`) and `firmware/lib/config_logic` (`cfg::parse`). Both report the first problem as `"<path>: <reason>"`, e.g. `pages[0].widgets[1]: overlaps another widget`.

- `schema` must be 1 and `pages` must exist; every other section may be missing (struct defaults). Unknown keys are ignored. A JSON `null` counts as missing.
- Numbers are range-checked (see below), whole numbers where the schema says so; strings have byte limits (UTF-8): page `id` 23, page `name` 31, widget `type` 19, `source` 39, `label` 31, `unit` 11, `text` 63.
- 1 to 8 pages, page ids unique and not empty (`name` defaults to the id), up to 16 widgets per page, at most 128 different data sources over the whole config (metric widgets, server cards and status bar items; the device keeps 128 SYS_STATS values, about 5 KB of RAM, and 128 ids still fit one SYS_STATS packet).
- Pages may set their own `grid` (`{cols, rows}`, 1 to 8) and `theme` (`{name, accent}`, each optional); otherwise the global ones apply.
- Widgets: known type, `x`/`y`/`w`/`h` inside the page's grid, big enough for the type (see "Widget catalog"), no overlap. `metric_*` need a `source`, `label` needs `text`, `image` needs `asset` (and `fit` is `cover` or `contain`), `weather_forecast` `mode` is `daily` (default) or `hourly`, `decimals` 0 to 3, and `min` must be below `max` after source defaults.
- Actions: known names; `nav.page`, `media.seek`, `media.volume`, `display.brightness` take a rotation and are only allowed on `rotate`/`rotate_pressed`; all others only on presses (`click`, `double`, `long`).
- Ranges: `display.panel` one of `ili9488`, `st7789`, `display.orientation` one of `landscape`, `portrait`, `landscape_flipped`, `portrait_flipped` (see "Screen"), `display.brightness` and `night.brightness` 0 to 100, `night.from`/`to` `"HH:MM"`, `spi_hz` 1 to 80 MHz, `grid` 1 to 8, `theme.name` one of `dark`, `midnight`, `black`, `graphite`, `theme.accent` `"#RRGGBB"`, `sensor.temp_offset_c` -10 to 10, `hum_offset_pct` -20 to 20, `trend_threshold_hpa` 0.1 to 10, `media.volume_step` 1 to 20, `seek_step_s` 1 to 60, `inputs.buttons` 0 to 4, `inputs.knob_ports` and `button_ports` (see below), `counts_per_detent` 1 to 8.
- Host only (the device ignores these sections): `weather.lat` -90 to 90 and `lon` -180 to 180 (both or neither), `refresh_min` 5 to 180, `media.art_size` 32 to 480, `media.app_priority` up to 16 names, `stats.rate_hz` 0.2 to 4.

Style fields a metric widget leaves out come from its source: temperatures `°C` 20 to 100 (warn 75, crit 90), loads `%` (80/95), `used_pct`/`vram_pct` `%` (85/95, disks 90/97), other `_pct` `%`, `_kbps` `kB/s` 0 to 10000, `_mhz` `MHz`, `_w` `W`, `_gb` `GB`, `rpm`. `"unit": ""` means no unit.

## Host and device copies

- Host: `Config/ConfigService.cs` loads `%APPDATA%\rez-pad\config.json`, migrates it (a missing section or section key is filled in from the default; files from before Phase 9 only had `weather` and `units`), validates it (an invalid file is kept as `config.json.bad` and the default is used) and writes atomically. The device gets the compact UTF-8 bytes; their CRC32 is the config's identity.
- `Device/ConfigSync.cs` pushes those bytes when HELLO_ACK `cfg_crc` differs and after every saved change, and waits for CONFIG_ACK (docs/protocol.md, "Config transfer").
- Device: `src/config/store.cpp` keeps the file in LittleFS (`/config.json`, written as `/config.tmp`, read back, renamed). At boot it loads and validates it; when it is missing it uses the built-in default, when it is invalid it uses the default and shows a warning in the status bar (the stored CRC stays, so the host pushes a good one). A pushed config is validated and saved on the link task, then the UI task rebuilds every page and applies inputs, sensor offsets, brightness, units, accent color and SPI clock.

Encoder gestures: `rotate`, `rotate_pressed` (turned while the knob is held down; that press then produces no click/double/long), `click`, `double` (when `none`, clicks fire on release without waiting for a second click), `long`. Buttons: `click`, `double`, `long`. Rotate gestures take an action name; press gestures (`click`, `double`, `long` of knobs and buttons) also take a key shortcut or a macro, see "Buttons and macros".

Pages may still carry `inputs_override` (same shape as `inputs`, only the keys to change) for page-specific mappings. The default config uses none: an override of `enc1.rotate` would make it impossible to leave that page by turning.

Knobs `enc1` to `enc3` and buttons `btn1` to `btn4` are **numbers**; which physical port each number reads is set by `inputs.knob_ports` and `inputs.button_ports`: arrays where position N-1 holds the port of number N (knob ports 1 to 3, button ports 1 to 4, docs/hardware.md), 0 = that number is not used, and numbers after the array's end are not used. Ports in use must differ. Defaults: `knob_ports` `[1, 2]`, no buttons. The older `inputs.buttons` (0 to 4, ports 1 to n) still works when `button_ports` is missing; the editor replaces it with `button_ports`. Mappings of numbers without a port are kept but do nothing. `counts_per_detent` and `invert` belong to the knob number; the editor moves them along when a physical knob gets another number (they describe that knob).

The editor sets the numbering in Settings > Knobs and buttons: "Assign" asks the device for the next control turned or pressed (CMD `input_learn`, `POST /api/device/learn`) and gives it that number; a number that had this control takes the old port of the number being assigned (a swap), or is left without a port. Gestures stay with the number.

`weather`: no location by default (`lat`/`lon` null): the user picks a city on the web page (Open-Meteo geocoding), and the host fetches nothing until then. `refresh_min` 5 to 180. `units`: `temp` `c`/`f`, `wind` `kmh`/`ms`/`mph`, `pressure` `hpa`/`inhg`; weather values are converted by Open-Meteo (pressure by the host), and the device shows its room sensor in the same units.

`media`: `app_priority` orders media sessions by app (matched case-insensitively as part of the app name or its AppUserModelId, `*` matches any). Which app Now Playing shows: the app in charge stays chosen while it has a session, playing or paused (the one that last started playing, or the one picked with `media.app_switch` / `media.app_next` or on the Settings page), so pausing an app keeps it on screen and play resumes it. An app that starts playing takes over. Only without an app in charge (host start, the app closed) does a playing session win over paused ones, then the list decides, then Windows' current session; the list also decides when several start at the same moment. The editor offers the apps seen on this PC (`%APPDATA%\rez-pad\media-apps.json`, host only) and lets you type any name. `art_size` is the album art edge in px when the device does not ask for one in HELLO_ACK. `volume_step` is percent per detent, `seek_step_s` seconds per detent.

## Widget catalog (firmware implements, web editor offers)

A widget may have any size (in cells) whose content area is at least the type's minimum. Content area = card minus 7 px padding on each side; a card is `w * floor(480 / cols) - 4` by `h * floor(300 / rows) - 4` px. On the default 4x3 grid a 1x1 card has a 102 x 82 px content area. Every widget picks fonts and parts to fit its size (firmware `src/widgets/`, mirrored by `web/preview.js`), so for example a 1x1 album art, a 2x1 forecast (4 columns) and a 1x1 media control (buttons only) all work.

| Type | Minimum content (px) | Data |
|---|---|---|
| `now_playing` | 150 x 60 | album art, title, artist and controls in one card: art left when wide (2x1, 3x2, 4x2), on top when tall (2x2, 2x3) |
| `album_art` | 40 x 40 | media art, square, centered |
| `track_info` | 80 x 18 | title (scrolls), then artist, album, app as height allows |
| `media_controls` | 60 x 24 | prev/play/next; progress bar from 44 px high, times from 70 px |
| `progress` | 60 x 10 | progress bar; times from 30 px high |
| `volume` | 40 x 34 | volume ring and level, mute icon from 64 px. `target`: `master` (default, the PC's volume) or `app` (the Now Playing app's own volume in the Windows volume mixer; its name replaces the icon from 90 px) |
| `metric_value` | 50 x 30 | any numeric source, label, unit |
| `metric_bar` | 60 x 30 | bar with min/max and thresholds; one row below 44 px high |
| `metric_gauge` | 44 x 44 | ring gauge; the number is sized to fit inside the ring, the unit in small type under it |
| `metric_graph` | 80 x 40 | sparkline history (device keeps a ring buffer per source) |
| `clock` | 50 x 26 | time, and the date when there is room |
| `room_climate` | 60 x 30 | temp, humidity, pressure, trend |
| `weather_now` | 60 x 34 | icon, temp, condition; location, age and details when big |
| `weather_forecast` | 90 x 46 | `daily` or `hourly`, one column per 48 px (up to 5) |
| `label` | 16 x 14 | static `text`, optional small `label` above it |
| `image` | 16 x 16 | uploaded picture or animated GIF (`asset`, `fit` `cover` or `contain`), edge to edge in the card |
| `mixer` | 110 x 50 | the Windows volume mixer (MIXER). `layout`: `list` (default, one row per app: name, level, bar) or `faders` (one column per app with a vertical bar and the name on up to two lines; from 100 px high, smaller cards use the list). `apps`: up to 8 app ids (from MIXER, e.g. `"spotify"`) shown first in this order; `only`: show just those. Interactive, see "Mixer widget" |
| `server` | 110 x 60 | a Beszel machine (`system`: its id, e.g. `"main-pve-node"`; `label`: the title): online dot, CPU / memory / disk bars as far as the height allows, then load, temperature and network down/up. Uses 8 sources (see "Integrations") |
| `spectrum` | 60 x 30 | audio spectrum bars of what the PC plays (SPECTRUM). `bars` 4 to 32 (default: one per 10 px of width, at most 32) |

Widgets may have optional style fields (`label`, `unit`, `min`, `max`, `warn`, `crit`, `decimals`); see "Validation" for the rules and defaults.

## Themes

`theme.name` picks a preset (screen, card, border, text and status bar colors), `theme.accent` the highlight color. Presets: `dark` (default), `midnight` (deep blue), `black` (pure black background), `graphite` (lighter grey cards). Their colors are in `firmware/src/ui/theme.cpp`, `web/preview.js` and, for the card color images are composited on, `host/src/RezPad.Protocol/Assets.cs`; change all three together. A page's own `theme` overrides either value for that page; the screen, status bar and every widget on it use the page's colors.

## Images

`image` widgets show pictures uploaded on the web page (`POST /api/assets`, PNG, JPEG, GIF or BMP up to 20 MB, kept in `%APPDATA%\rez-pad\assets`). The host converts each one to the widget's exact size and the page's card color and sends it before the config (docs/protocol.md, "Assets"). Animated GIFs play on the device: frames shorter than 50 ms are merged, and frames are dropped evenly so each widget stays under 2 MB of decoded frames and all of them under 4 MB.

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

`none`, `nav.page` (rotate), `nav.next_page`, `nav.prev_page`, `nav.home`, `media.play_pause`, `media.next`, `media.prev`, `media.seek` (rotate, seconds per detent configurable), `media.volume` (rotate), `media.mute`, `media.app_volume` (rotate), `media.app_mute`, `media.app_switch` (rotate), `media.app_next`, `mixer.focus`, `display.toggle`, `display.brightness` (rotate).

`mixer.focus` (press) focuses the `mixer` widget on the current page, or unfocuses it; see "Mixer widget".

`media.app_switch` (each detent) and `media.app_next` move Now Playing to the next app with a media session (order: the preferred apps list, then name); the old app keeps playing, only the screen and the media keys follow. They need the host: offline they do nothing.

`media.app_volume` and `media.app_mute` change only the app that is playing (the Now Playing app, e.g. Spotify or the browser), like its slider in the Windows volume mixer, by `media.volume_step` per detent. The host matches the media session to the app's audio sessions by process (and package for Store apps) and changes all of them. They need the host: offline they do nothing (there is no media key for one app's volume).

### Knob popup

When a knob runs `media.volume`, `media.mute`, `media.app_volume`, `media.app_mute`, `media.seek` or `display.brightness` and the shown page has no widget for that value, the device shows a small card in the middle of the screen with the live value (icon or album art, name, level or position and a bar) for 3 s after the last change. A `volume` widget with the same `target` covers the volume actions, a `progress`, `media_controls` or `now_playing` with a progress bar covers seek; brightness always shows it. The card stays up across page switches unless the new page shows the value. Host-only actions (app volume, seek) show nothing offline; offline volume shows `--` (the knob still sends media keys). Turns that a focused mixer takes, and every other action, show nothing. `display.knob_popup` (bool, default `true`) turns it off; added within schema 1, a config without it gets `true` (the host fills it in from the default).

## Integrations

Apps on the user's network the host reads (Settings > Integrations). Their settings and credentials are host-only (`%APPDATA%\rez-pad\integrations.json`, password encrypted with Windows DPAPI); the config only names their data sources.

**Beszel** (system monitoring hub): the host signs in as a hub user (best a read-only one; it sees the systems shared with it) and offers each machine's values as sources `bz.<machine>.<metric>`, `<machine>` being a short id from its name (a-z, 0-9, -, up to 14 characters):

| Metric | Unit | |
|---|---|---|
| `online` | | 1 up, 0 down |
| `cpu_load`, `mem_used_pct`, `disk_used_pct` | % | |
| `mem_used_gb`, `disk_used_gb` | GB | per-minute record |
| `load1`, `load5`, `load15` | | load average |
| `net_kbps`, `net_rx_kbps`, `net_tx_kbps` | kB/s | total, down, up (down/up per minute) |
| `temp` | °C | the temperature Beszel shows |
| `cpu_temp` | °C | the CPU sensor (package, Tctl or cpu_thermal) |
| `uptime_h` | h | |
| `fs1_used_pct` to `fs3_used_pct` | % | extra filesystems, sorted by name |

They work with every metric widget and status bar item; the `server` widget shows `online`, `cpu_load`, `mem_used_pct`, `disk_used_pct`, `load1`, `temp`, `net_rx_kbps` and `net_tx_kbps` of one machine, and those 8 count toward the 128 different sources. A machine that is down keeps its sources without values ("--").

## Status bar

The 20 px bar shows the clock, the page name, the page dots and the host icon. The dots show at most 5 pages at a time: the window slides with the current page, and a smaller dot at an end means there are more pages that way.

`status_bar.items` (optional, up to 3) adds small items on the right, on every page, each `{"type": ..., "label": "..."}` with an optional short `label` (up to 7 bytes, e.g. "CPU") in front of the value:

| `type` | Shows |
|---|---|
| `date` | weekday and day, e.g. "Sun 8" |
| `room_temp` / `room_hum` | the device's room sensor (temperature in the config's units) |
| `outdoor_temp` | current weather temperature |
| `volume` | the PC's volume (mute icon when muted) |
| `metric` | any data source, `source` required (e.g. `{"type": "metric", "source": "sys.cpu.temp", "label": "CPU"}`); counts toward the 16 different sources |

While the mixer widget is focused, or for 5 s after a page with a mixer is shown, the bar shows a hint (what the knobs do, or which gesture focuses the mixer) instead of the clock, name, dots and items.

## Mixer widget

The `mixer` widget lists the apps in the Windows volume mixer (sent by the host, MIXER). It does nothing until focused:

- A press mapped to `mixer.focus` focuses it (accent ring; the selected app gets an accent background and edge, the others dim). Map it to a button or a knob gesture; the editor's mixer settings offer to map a free gesture in one click, and the device's status bar says which gesture to use (or that none is mapped) when the page is shown.
- While focused, the knobs drive the mixer instead of their mapping: **knob 1 turn** selects the previous/next app, **knob 2 turn** changes the selected app's volume (`media.volume_step` per detent), **knob 2 click** mutes or unmutes it, **knob 1 click** or the `mixer.focus` press again unfocuses. Other gestures (double clicks, long presses, press-and-turn, buttons) keep their normal actions.
- Knob 1 moves through the widget's own order (its `apps` first) and focus starts at the top of it. Only the first mixer widget of a page can be focused.
- It also unfocuses after 15 s without input, on a page change, on a new config and when the host disconnects.
- Without the host it shows "No host connected" and cannot be focused.

## Buttons and macros

A press gesture is one of:

- an action name (above), e.g. `"media.mute"`;
- `{"keys": "ctrl+shift+m"}`: a key shortcut the **device** types through its own USB keyboard (a second HID device next to the media keys). Works without the host app, on any PC. Keys are positions, like a physical keyboard: the same shortcut does the same as pressing those keys on the PC's keyboard;
- `{"macro": "<id>"}`: a macro from the `macros` list, run by the **host** (needs the app running; offline the press does nothing).

Key names, case-insensitive, joined with `+`: modifiers `ctrl`, `shift`, `alt`, `win`, `altgr` (right Alt), then at most one key: `a` to `z`, `0` to `9`, `f1` to `f24` (`f13` to `f24` are not on most keyboards, so apps can bind them without clashing), `enter`, `esc`, `backspace`, `tab`, `space`, `minus`, `equal`, `lbracket`, `rbracket`, `backslash`, `semicolon`, `quote`, `grave`, `comma`, `period`, `slash`, `capslock`, `printscreen`, `scrolllock`, `pause`, `insert`, `home`, `pgup`, `delete`, `end`, `pgdn`, `up`, `down`, `left`, `right`, `numlock`, `num0` to `num9`, `numdiv`, `nummul`, `numsub`, `numadd`, `numenter`, `numdot`, `intl` (the extra key next to left Shift on ISO keyboards, e.g. `<` on Turkish Q), `menu`. Modifiers alone (`"ctrl+shift"`) are allowed. Parsed by `actions::parseKeys` (firmware) and `KeyCombo` (host) with the same table.

`macros` (host, up to 32): `{"id": "obs", "name": "OBS scene", "steps": [...]}`. `id` 1 to 23 bytes, unique; `name` up to 31 bytes. 1 to 32 steps, each one of:

- `{"keys": "ctrl+f1"}`: a shortcut, sent with SendInput;
- `{"text": "..."}`: types the text (up to 500 bytes) as Unicode characters, right on any keyboard layout; a new line presses Enter;
- `{"wait_ms": 200}`: 1 to 10000 ms;
- `{"open": "..."}`: a program, file, folder or web address (up to 260 bytes). When the host runs elevated it starts it through Explorer, so it gets normal user rights (no arguments in that case).

The device only reads the macro ids (validation of `{"macro"}` references); the steps are checked by the host. Macros run one at a time in the order the presses arrived. The device sends ACTION `{"action": "macro", "value": 1, "id": "<id>"}`.

## Web config editor

`web/` in the repo, embedded in the host exe (or served from the folder in `REZPAD_WEB_DIR` while developing) and served at `http://127.0.0.1:47800`. Plain HTML/CSS/JS as ES modules, no build step and no third-party code. Three views:

- **Layout**: pages on the left (live thumbnails, new page), the device screen in the middle and the inspector on the right. On the screen: drag a widget to move it (dropping it on one other widget swaps the two when both fit), drag the corner of the selected widget to resize, click an empty cell to add a widget there; arrow keys move, Shift + arrows resize, Delete removes. The selected widget has a remove button on its top right corner. Above the screen: the page name (a title with a pencil; click to rename in place, Enter keeps it, Escape goes back) and the page's grid, colors, knob overrides and more (duplicate, reorder, delete). The inspector shows the widget gallery (live previews; click to add, or drag onto the screen) or the selected widget's settings: data source browser (step by step: where the value comes from, This PC or a Beszel machine (skipped when there is only this PC), then a category, then the value, all as tiles with live values; opens where the current value is, with a path back up and search across everything; `web/sources.js`, also used for status bar items; a new metric widget starts without a value and the browser opens right away, a new server card without a machine), labels, size chips (every size that fits), look (unit, range, thresholds, decimals), forecast mode, text, image picker with upload.
- **Settings**: categories in a left column, one shown at a time (the choice is remembered in the browser): Look (appearance: theme presets and accent; screen; status bar), Controls (knob and button numbering, what they do; macros), Data (weather and units; room sensor; media and system data; integrations).
- **Device**: status, firmware (the board's version next to the one built into this host; update or reinstall over the same USB cable, or install a `.bin` file; also installs onto a board waiting in download mode; `web/firmware.js`), compare with the device's copy, restart, clear the device's config, the uploaded images, the data sources of this PC.

The app bar shows the logo (its screen lights up while the board is connected), the three views, a device pill (connection and sync state, "update available", update progress; it opens the Device view), undo, redo and save.

Files: `app.js` (loading, polling, validation, saving, views), `state.js` (working copy, undo history, API, popover, toasts), `editor.js` (Layout), `preview.js` (device-look rendering, themes, widget catalog for the UI), `settings.js` (Settings, Device), `icons.js`.

Every change is checked by the host (`POST /api/config/validate`, the same validator as saving); an error shows in a banner with "Show me", and Save stays disabled. Ctrl+Z / Ctrl+Y undo and redo, Ctrl+S saves. Save sends `PUT /api/config?base=<crc>`: the host refuses (409) when the config changed elsewhere since the page loaded, otherwise it saves, sends images and config to the device and the page shows the device's answer. Live data is polled (status every 1.5 s, stats and media every 1 s, weather and room every 5 s, paused while the tab is hidden).

## Host API (localhost only)

- `GET /api/status` (includes `config`: host CRC, device CRC, in sync, last push result).
- `GET /api/config` (current config, its CRC in the `X-Config-Crc` header), `PUT /api/config[?base=<crc>]` (validates, saves, pushes; `400 {error}` with the validator's message, `409` when `base` is not the current CRC, otherwise `{saved, crc, device}` where `device` is the push result or null when no device is connected), `POST /api/config/validate` (`{error, bytes}`, saves nothing), `GET /api/config/default`.
- `GET /api/catalog`: widget types with allowed sizes, actions and which of them take a rotation, limits, and every data source on this PC with its style defaults.
- `GET /api/device/config` (the device's stored copy via CONFIG_GET), `POST /api/device/cmd`.
- `POST /api/device/learn?ms=` (waits for the next control turned or pressed: `{kind, port, error}`), `DELETE /api/device/learn` (cancels).
- `GET /api/firmware` (`bundled` image version, `device` version, `bootloader_port` of a board in download mode, `update_available`, update `status`), `POST /api/firmware/update` (the bundled image, or a `.bin` sent as multipart field `file`; runs in the background, `status` shows the stage and progress).
- `GET /api/assets` (uploaded images), `POST /api/assets` (multipart field `file`), `GET /api/assets/{id}` (the original), `DELETE /api/assets/{id}` (refused while a widget uses it).
- `GET /api/media/apps` (apps with a media session now, the Now Playing one marked, and every app seen before), `POST /api/media/apps/choose?id=` (make one the Now Playing app).
- `GET /api/sources`, `GET /api/media`, `GET /api/media/art?size=` (current album art as JPEG), `GET /api/weather`, `PUT /api/weather`, `GET /api/geocode?q=`, `GET /api/sensor`, `GET /api/hardware`, `GET /api/log`.
- Bind to `127.0.0.1` only. No remote access.
