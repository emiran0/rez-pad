# Changelog

All notable changes, newest first. Version rules: [docs/versioning.md](docs/versioning.md).

## 0.4.0 (2026-11-10)

Update the board from the Device page after updating the host (new knob actions; the USB protocol is still v1, the config schema still 1).

### New
- **Switch the Now Playing app from the device**: knob actions "Switch media app" (turn) and "Next media app" (press) move Now Playing to the next app with media; the app that was playing keeps playing.
- Settings > Media and system data: a **Now Playing card** listing the apps with media on this PC (switch with a click), and the **preferred apps** as an ordered list: drag or arrows to reorder, add apps seen on this PC (also while they are closed) or type a name, "Any other app" as its own row.

### Changed
- Now Playing **stays on the app you used last**, also when you pause it, so play/pause resumes that app; an app that starts playing takes over. The preferred apps list only decides after a restart, when the app on screen closes, or when several start at once. (Before, pausing the browser handed the screen to the first preferred app, and the next play started that one.)
- Sliders have a new look: filled track, round thumb, value in a pill.

## 0.3.1 (2026-11-09)

Host only (the editor); the firmware is the same as 0.3.0 apart from its version number. Update the board from the Device page anyway to keep the versions in step.

### Fixed
- Adding a value, bar, gauge, graph or server widget showed a "Can't save yet" error right away, before there was a chance to pick its value or machine. That error now waits until you try to save; then it shows and takes you to the widget.

## 0.3.0 (2026-11-09)

Flash the new firmware together with this host, once more with esptool or the web flasher if the board runs 0.2.0 (0.2.0 cannot restart into update mode by itself; from 0.3.0 on, updates run from the config page). The USB protocol is still v1, the config schema still 1.

### New
- **Firmware updates from the config page** (Device > Firmware): the host has the matching firmware built in and writes it over the same native USB cable through the chip's built-in loader (about 35 s, checked with MD5). Layout, settings and pictures stay. It can also install a `.bin` file, and installs rez-pad onto a new board or one held in update mode (BOOT while plugging in).
- **Knob and button numbering** (Settings > Knobs and buttons): choose which physical knob or button is Knob 1, Button 2 and so on by clicking Assign and turning or pressing it. Numbers swap when a control already had one; actions stay with the number.
- **Third knob**: knob port 3 on GPIO 39, 40, 41 (not yet verified on a board).
- Up to **128 data sources** (was 32), enough for several Beszel server cards plus a full PC page.
- Editor: new top bar with the logo, a device pill (connection, sync, update available, update progress), undo and redo buttons; page names as a title with a pencil to rename in place; a remove button on the selected widget.

### Changed
- New value, bar, gauge and graph widgets start without a data source and open the source browser right away; new server cards start without a machine.
- The source browser starts at the categories when there is only this PC (no Beszel).
- Setup guide: install the app first, then install the firmware from the config page.

## 0.2.0 (2026-11-09)

Flash the new firmware together with this host (new widgets and messages; the USB protocol is still v1).

### New
- **Spectrum** widget: bars that move with whatever the PC plays (WASAPI loopback on the host, about 30 frames per second).
- **Volume mixer** widget: the apps in the Windows volume mixer as rows or vertical faders. Focus it with a button, pick an app with knob 1, set its volume with knob 2; a priority list keeps chosen apps on top (or shows only them). The status bar explains the knobs while it is focused, and the editor maps the focus gesture in one click.
- **Per-app volume**: knob actions for the playing app's volume and mute; the volume widget can show the playing app.
- **Status bar items**: up to 3 small items next to the clock (date, room or outdoor temperature, volume, any system value); the page dots show at most 5 pages and slide.
- **Integrations** (Settings > Integrations), starting with **Beszel**: each machine's CPU, memory, disk, load, network, temperatures, uptime and extra disks as data sources, and a **server card** widget. Credentials stay on the PC, encrypted.
- Data sources are picked with a **step-by-step browser** (where, category, value) with live values and search.
- Settings page with **categories** and refreshed controls; Layout page settings (grid, colors, knobs and buttons) inline under the preview; the preview fills large monitors.
- Up to **32 data sources** (was 16).
- Console: `perf` (frame rate and SPI load), `mixer`, `input` (inject a knob gesture), `screen`.

### Changed
- Animated images keep every frame (only thinned to fit the 1 MB file limit); long animations decode frame by frame on the device instead of being cut down to fit memory.
- Size options include sizes that grow left or up (widgets at the right or bottom edge).
- Open source under MIT; file details name rez-lab.

### Fixed
- Every message from the host waited about 31 ms for the serial read (now 0.3 ms): faster album art and config transfers.
- A spectrum with many bars redrew the whole screen every frame (5 FPS); now about 29 FPS.
- A 120-frame GIF played at about 1.4 FPS.

## 0.1.0 (2026-11-08)

First installable version (roadmap phases 0 to 11): ESP32-S3 device with a 3.5" display, two knobs, optional macro buttons and a room sensor; Windows tray host with Now Playing (album art, controls, seek, volume), system stats, weather, room climate, macros and key shortcuts; widget grid editor in the browser; night dimming and screen off while the PC sleeps; single-file host with a per-user installer.
