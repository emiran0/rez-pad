# Measurements

Record real numbers from the hardware here. Include date, firmware commit and conditions.

## SPI clock (ILI9488)

The ESP32-S3 SPI clock is 80 MHz / n, so the only steps near the targets are 80, 40, 26.7 and 20 MHz. A 60 MHz request runs at 40 MHz.

`bench` console command, average of 5 runs. "Strips" pushes a full 480x320 frame as eight 480x40 RGB565 DMA strips, which is how LVGL will flush. "Ideal" is 480 x 320 x 24 bit / clock.

| Date | Commit | Requested | Effective | Fill (ms) | Ideal (ms) | 200x200 (ms) | Strips frame (ms) | Artifacts? | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 2026-11-07 | Phase 1 | 20 MHz | 20 MHz | 187.5 | 184.3 | 48.2 | 184.9 | not checked | Timings measured while the display was still unpowered (5V pin issue); bus-side only, valid for throughput |
| 2026-11-07 | Phase 1 | 40 MHz | 40 MHz | 95.4 | 92.2 | 24.2 | 92.7 | **clean** | Visual check with display on 3V3: colors, orientation, edges, ramps, checkerboard all correct |
| 2026-11-07 | Phase 1 | 60 MHz | 40 MHz | 95.4 | 92.2 | 24.2 | 92.7 | clean | Same clock as 40 MHz |
| 2026-11-07 | Phase 1 | 80 MHz | 80 MHz | 48.8 | 46.1 | 12.3 | 46.7 | **glitches** | Visible pixel errors over breadboard jumpers. Retest with short soldered wiring in the final build. |

Bus throughput is within 1 to 3% of the theoretical limit at every clock. Breadboard jumper wires, 10 to 15 cm.

Chosen default: **40 MHz** (2026-11-07). Full frame 92.7 ms, within the < 120 ms target. 80 MHz glitches on the breadboard; worth retesting once the wiring is short and soldered.

## Performance targets

| Metric | Target | Measured | Date |
|---|---|---|---|
| Track change to text on screen | < 150 ms | 23 to 36 ms host send to widget update, plus GSMTC event (~15 ms) and one frame (33 ms) | 2026-11-07 |
| Track change to album art on screen | < 600 ms | about 400 to 550 ms (200 ms debounce on the host, encode, 100 to 170 ms transfer, 14 to 18 ms decode) | 2026-11-07 |
| Encoder input to host action executed | < 100 ms | ACTION to Spotify state change 41 ms (host log) | 2026-11-07 |
| Full page redraw at 40 MHz SPI | < 120 ms | 92.7 ms (480x40 DMA strips) | 2026-11-07 |
| Device recovers after host restart | < 3 s | | |
| Host recovers after device unplug/replug | < 3 s | | |
| Host average CPU | < 1% | 0.10 % of 12 cores (12 ms CPU per second) with 1 Hz system stats (direct NVML + D3DKMT) and media; 0.29 % with LibreHardwareMonitor for the GPU; 0.07 % before Phase 7 | 2026-11-07 |
| Host memory | < 150 MB | 147 MB working set, 61 MB private (debug build, Phase 7). The working set is mostly mapped images shared with other processes: Microsoft.Windows.SDK.NET (WinRT projection, 24 MB), nvml.dll (22 MB), CoreLib, WinForms. Was 90 MB before LibreHardwareMonitor; most of the jump is NVIDIA driver DLLs mapped into the process. Recheck with a Release build in Phase 11 | 2026-11-07 |

## Boot log

| Date | Commit | PSRAM size | Flash size | Free heap | Notes |
|---|---|---|---|---|---|
| 2026-11-06 | 7e70ad7 | 8192 KB (8188 KB free) | 16 MB | 347 KB internal | ESP32-S3 (QFN56) rev v0.2, 240 MHz, embedded PSRAM AP_3v3. Flashed over the CH343 bridge at 921600 baud (17.9 s). Protocol self-test ok. Phase 0 check passed. |

## Encoders

| Encoder | Counts per detent | Direction OK? | Notes |
|---|---|---|---|
| enc1 | 4 | yes (clockwise = +) | 2026-11-07, breadboard. 10 slow clicks each way = exactly 10 steps each way. Fast back-and-forth spin: +116 / -116 steps, net 0, no queue drops. Click, double and long each detected once. |
| enc2 | | | Not fitted yet |

## Sensor calibration

| Date | Reference temp | AHT20 temp | Offset applied | Reference RH | AHT20 RH | Notes |
|---|---|---|---|---|---|---|
| 2026-11-07 | none | 28.3 C raw at boot, 29.2 C after ~5 min (BMP280 29.4 C) | -1.5 C default | none | 33.1 % raw, about 35 % after the temperature correction | Breadboard, sensor near the ESP32 and display: it warms up after boot (self-heating). Pressure 1018.2 hPa, 0 read errors in 22 samples. Calibrate once the sensor sits in its final position. |

## UI rendering (LVGL)

LVGL sysmon overlay (`perf 1`), 30 FPS cap (LV_DEF_REFR_PERIOD 33 ms), SPI 40 MHz, 2 x 480x40 RGB565 draw buffers.

| Date | Page / situation | FPS | CPU (UI core) | Notes |
|---|---|---|---|---|
| 2026-11-07 | System, Weather & Room (idle, 1 Hz stats) | 45-50 | 3-24 % | before the 30 FPS cap |
| 2026-11-07 | Now Playing, title + artist + album all scrolling | ~25 | 50-55 % | first version, 3 marquees at 40 px/s |
| 2026-11-07 | same, 3 marquees at 20 px/s | | 80-85 % | worse: labels step on different frames, more refresh passes |
| 2026-11-07 | same, only the title scrolls (30 px/s = 1 px per frame) | 30 (cap) | ~40 % | current. Remaining load is mostly waiting for SPI inside the flush. |

Page switch: reported as instant on the hardware. Possible later optimization if needed: convert RGB565 to RGB666 into a separate DMA buffer and return from flush immediately, so the next strip renders while the previous one is on the wire.

## USB (Phase 4)

| Date | Commit | Result |
|---|---|---|
| 2026-11-07 | Phase 4 | Windows 11 enumerates "rez-pad" (303A:4005, serial RP-ACA704F47EDC) as USB Composite Device with COM6 (usbser) and a HID consumer control device, standard drivers only. Media keys from encoder 2 (play/pause, next, volume) work with no host app: 39 keys sent, 0 dropped. |

## Host link (Phase 5)

Windows 11, host app debug build, board on both USB cables (native + CH343).

| Date | Scenario | Result |
|---|---|---|
| 2026-11-07 | Host start to connected (discovery, HELLO, HELLO_ACK, TIME_SYNC) | 0.7 to 0.8 s |
| 2026-11-07 | Device reset (RTS pulse or CMD reboot) to host reconnected | 1.5 to 1.6 s, 6 of 6 runs (boot included) |
| 2026-11-07 | Host app killed: device notices | ~3.1 s (PING timeout), device keeps running |
| 2026-11-07 | Host restart to connected | 0.7 s |
| 2026-11-07 | Native cable unplug/replug | host connected 0.2 s after COM6 reappeared |
| 2026-11-07 | Clock | status bar shows the PC's local time after TIME_SYNC, keeps running when the host is gone |

**Known issue, likely cause found in Phase 6 (see below):** twice during heavy stress testing (many rapid resets through COM4, repeated force-kills of the host with reads in flight) Windows entered a state where *every* close of COM6 took 30.0 s, also with pyserial, so not specific to our host. The board kept working and never received the close's SET_CONTROL_LINE_STATE during those 30 s (matches https://esp32.com/viewtopic.php?p=155679). A full power cycle of the board cleared it both times. Not reproduced by: cold boot, software reboot, native-only cable replug, normal app exit/restart, force-kill on a healthy link. COM4 (CH343) also wedged ("Incorrect function") in the same episodes until replugged. Mitigations in place: the host releases its single-instance lock before closing the port, and a new instance retries a busy port every 0.5 s, so a restart connects as soon as Windows lets go. Revisit if it shows up in normal use (watch the host log for `link closed in 2000+ ms`).

## Now Playing (Phase 6)

Windows 11, host debug build, Spotify desktop and a Brave tab (YouTube). Art 192 px (the default layout's album_art card asks for 192 in HELLO_ACK).

| Date | Item | Result |
|---|---|---|
| 2026-11-07 | NOW_PLAYING to widget update | 23 to 36 ms after the host sampled it (device log, synced clock) |
| 2026-11-07 | Art size and transfer | Spotify 145 KB thumbnail to 7.7 KB JPEG; YouTube thumbnail to 9.1 KB. Sent in 100 to 170 ms |
| 2026-11-07 | Art decode (JPEGDEC 1.8.5, 192x192 RGB565 into PSRAM) | 14 to 18 ms |
| 2026-11-07 | Art on screen after the track text | 135 to 304 ms |
| 2026-11-07 | Actions through the device console (same path as the knobs) | volume +/-3 detents (40 to 46 to 40), mute/unmute, play/pause, seek +30 s / -20 s on Spotify: all correct |
| 2026-11-07 | Device reboot while the host runs | reconnected 1.6 s later, art pushed again |
| 2026-11-07 | Host CPU / memory | 0.07 % / 90 MB working set |

**Art transfer rate is about 80 KB/s.** Arduino's `USBCDC` passes every received byte through a FreeRTOS queue (`xQueueSend` per byte in the USB callback, `xQueueReceive` per byte in `read()`), roughly 12 us per byte. Fine for art (8 to 20 KB) and config; replacing the Arduino CDC class with direct TinyUSB reads would lift it if ever needed.

**Hub finding for the 30 s close issue.** It came back on the first runs with Phase 6 (host closes taking 2 s, then 30.0 s, and COM4 "Incorrect function", COM4 and COM6 both wedged until a power cycle). Both cables were plugged into the same Genesys Logic USB 2.0 hub (`VID_05E3&PID_0610`). Both board USB devices are full speed, so behind that hub they share one transaction translator, which explains why two independent chips always wedged together. After moving the native cable to a PC port directly (CH343 still on the hub), the same workload (host start, art transfer, actions, host exit with the debug console open on COM4) closed cleanly (34 ms, DTR and RTS changes reached the device 3 ms apart). Only a few runs so far; keep the native cable off that hub, and record it here if it ever happens on a direct connection. The final device only uses the native port.

## System stats (Phase 7)

Ryzen 5 5600X, RTX 3070 Ti, ASUS TUF B450M-PRO GAMING, 32 GB, Windows 11. Host not elevated, PawnIO not installed.

| Date | Item | Result |
|---|---|---|
| 2026-11-07 | Catalog | 20 sources (CPU, RAM, network, GPU x9, disk C:), ready 1.1 s after start |
| 2026-11-07 | Without PawnIO / admin | CPU temperature and power read as 0 from LibreHardwareMonitor (now treated as unavailable, sent as null, shown as `--`); motherboard has 0 sensors (no fans). Tray and web page show the PawnIO / admin hint |
| 2026-11-07 | GPU without admin | load, temperature, hot spot, clock, power, VRAM, fan: all read (NVAPI) |
| 2026-11-07 | Sample values | CPU 5 to 17 %, 4.2 GHz, RAM 46 % (14.7 of 31.9 GB), GPU 49 C idle. To compare with Task Manager on the device |
| 2026-11-07 | Host CPU / memory | 0.34 % / 156 MB working set, 69 MB private |
| 2026-11-07 | COM close times (native port direct to the PC) | 30 to 34 ms over 3 host restarts |

With PawnIO 2.x installed and the host started elevated by the "Start with Windows" task (2026-11-07): CPU Tctl/Tdie 62 to 64 C, CCD1 62 C, package power 49 W, per-core power; motherboard SuperIO ITE IT8655E: fans #1 1445 rpm, #2 0 (empty header), #3 1383 rpm, six unlabeled temperatures (#1 follows the CPU, #2 39 C, #3 to #6 46 C; not exposed as sources until they can be named). Host 0.11 % CPU (12.8 ms/s), 58 MB private.

**AHT20 missing (2026-11-07, Phase 7 bring-up), resolved.** For a while the I2C scan found only the BMP280 (0x77) and room temperature/humidity read 0. After later resets and power cycles it answers again at 0x38: 28.9 C raw, 29.7 % RH, 0 errors in 120 samples. Sensor code was never changed; most likely a loose contact on the breadboard or a stuck AHT20. Watch for `ok aht 0` in the `sensor` console output.

**GPU load fix (2026-11-07).** The first version used LibreHardwareMonitor's "GPU Core" load (NVIDIA's graphics-engine figure): 1 % on the device while Task Manager showed 18 % (video decode). `sys.gpu.load` is now the busiest D3D engine, as in Task Manager; during YouTube playback it reads the decode engine (8 to 9 %), core load 10 to 12 %.

**Where the host CPU goes (2026-11-07, thread CPU time per 1 s sample, 60-sample average):**

| Part | CPU ms per sample |
|---|---|
| LibreHardwareMonitor GPU update (all 39 sensors: NVAPI, NVML, D3D engine statistics) | 32.8 |
| Network adapter enumeration (`GetAllNetworkInterfaces`) | 5.7, now cached for 30 s, about 0 |
| LibreHardwareMonitor CPU update (no PawnIO) | about 0 |
| Windows CPU counters | 0.5 |
| Whole process | 35 to 39 ms per second |

Direct NVML calls (nvml.dll from the NVIDIA driver) cost about 2 us each for temperature, power, clock, utilization and VRAM (fan 0.27 ms); `nvmlDeviceGetPcieThroughput` waits 30 ms but uses no CPU. So reading the GPU without LibreHardwareMonitor (NVML plus the D3DKMT engine statistics Task Manager uses) would cut most of the 33 ms. Only the layout's hardware groups are refreshed now (CPU, GPU, motherboard), and everything only while the web page asks for the full catalog.

**Direct GPU readers (2026-11-07).** NVML for the NVIDIA values and D3DKMTQueryStatistics per engine node for the loads, matched to the same GPU by PCI bus/device. Checked against LibreHardwareMonitor side by side during YouTube playback: load 8.4 to 9.0 vs 8.5 to 8.7, decode identical, 3D 0.4 vs 0.4, temperature, power, clock and VRAM identical. Host CPU 35 ms/s to 11.6 to 12.2 ms/s. The D3DKMT struct sizes were taken from LibreHardwareMonitor's CsWin32-generated types at runtime (`D3DKMT_QUERYSTATISTICS` 808 bytes, `D3DKMT_NODEMETADATA` 78 bytes; 80 is rejected with STATUS_INVALID_PARAMETER). This adapter has no registry info (`KMTQAITYPE_ADAPTERREGISTRYINFO` returns STATUS_OBJECT_NAME_NOT_FOUND), so the name comes from `KMTQAITYPE_DRIVER_DESCRIPTION`.

**CPU load floor check (2026-11-07).** Reported: the device never shows CPU below 3.0 % while Task Manager sometimes shows 2 to 3 %. Compared for 60 s, once per second: the device value (debug console `stats`) against `typeperf` on the same PC. The device follows "% Processor Utility" (Task Manager's counter) with about 1 s lag: device 3.1 to 18.7 % (avg 6.75), Utility 2.7 to 16.8 % (avg 6.68). "% Processor Time" is lower at idle (1.3 to 3 %), since Utility also counts boost frequency above base clock. Idle Utility on this PC sits at about 3.1 to 3.5 %; Task Manager rounds to whole percent and samples at a different moment. No bug.

**Start with Windows (2026-11-07).** Tray item used by the user: one UAC prompt, task `rez-pad` created (logon trigger, highest privileges, user lolhe, interactive), host restarted through it elevated; `--exit` from a non-elevated shell still stops the elevated instance.

**Host log seen from a Claude desktop session.** The Claude desktop app is an MSIX package, so processes started from it (shells, and hosts launched from them) get a private copy of `%LOCALAPPDATA%` (`%LOCALAPPDATA%\Packages\Claude_*\LocalCache\Local\rez-pad\host.log`). A host started by Task Scheduler writes the real `%LOCALAPPDATA%\rez-pad\host.log`, which such a session cannot see. Use `GET /api/log` instead. `Log` now catches every exception and reports file problems as `logFileError` in `/api/status`.

## Weather and room (Phase 8)

| Date | Item | Result |
|---|---|---|
| 2026-11-07 | Location set through `PUT /api/weather` (Istanbul, 41.01384, 28.94966; units c / m/s / hPa) | Config saved, forecast fetched 0.2 s later: 10 C, code 2, wind 5.7 m/s, 5 days, 12 hours from the current hour |
| 2026-11-07 | WEATHER on the device | Received right after connect (`stats` console: Istanbul 10.0, code 2, 5 days, 12 hours) |
| 2026-11-07 | SENSOR to the host | Every 10 s: 26.6 C, 32.6 % RH, 1021.6 hPa, trend unknown (needs 1 h of history after a reboot) |
| 2026-11-07 | Firmware size | 1.49 MB of the 6 MB app partition (weather icons about 300 KB) |

Not checked on the screen yet: icon placement and readability, the `clock` widget and the forecast `hourly` mode (neither is in the default layout; they become placeable with the config in Phase 9/10).

## Config (Phase 9)

| Date | Item | Result |
|---|---|---|
| 2026-11-07 | First connect after flashing (device on the built-in default, `cfg_crc` 0) | Host migrated its pre-Phase 9 file (kept Istanbul and m/s), pushed 2206 bytes; device validated and saved in 27 ms, rebuilt 3 pages in 108 ms; CONFIG_ACK 262 ms after the push started |
| 2026-11-07 | Device reboot with a stored config | Loaded `/config.json` at boot, CRC matched on connect, no push |
| 2026-11-07 | `PUT /api/config` with overlapping widgets | 400 `pages[0].widgets[1]: overlaps another widget`, nothing sent |
| 2026-11-07 | `PUT /api/config` with a 4th page (clock, label with Turkish text, 2x2 gauge, CPU clock, room 2x1), hourly forecast, accent #FF7043 | Device applied 2762 bytes, 259 ms round trip; `sys.cpu.clock_mhz` added to SYS_STATS; CONFIG_GET readback identical (same CRC) |
| 2026-11-07 | CMD `factory_reset` | Device restarted on the default, host pushed again on reconnect, applied 2.0 s after the command |
| 2026-11-07 | Memory after boot | Internal heap 202 KB free, PSRAM 7834 KB free; firmware RAM 31.4 %, flash image 1.56 MB |

## Config editor and LVGL memory (Phase 10)

| Date | Item | Result |
|---|---|---|
| 2026-11-08 | Editor: add a page, add 4 widgets, drag, resize, override a knob, save | Device applied it in 299 ms; drag, resize, live validation (save blocked while invalid) and the "changed elsewhere" conflict all worked |
| 2026-11-08 | Internal heap with the default 3 pages, LVGL on malloc | 68-77 KB free (the 202 KB printed at boot is before the UI is built) |
| 2026-11-08 | 8 pages of 12 widgets (96), LVGL on malloc | 3 KB free: about to fail |
| 2026-11-08 | Same 96 widgets, LVGL on PSRAM (`LV_STDLIB_CUSTOM`) | 115 KB free; default 3 pages: 117 KB free |
| 2026-11-08 | Full page redraw (`page <n>`, System and Weather pages, 40 MHz SPI) | 152-155 ms on PSRAM, 153-157 ms on malloc: same, the SPI transfer dominates. Above the 120 ms target, see known-issues.md |
| 2026-11-08 | Repeated config pushes | Heap stays flat (68, 67, 67 KB over 3 pushes): no leak in the rebuild |

## Widget sizes, themes and images (2026-11-08)

| Item | Result |
|---|---|
| GIF 200x150, 16 frames, 24 KB, and a PNG 300x160 uploaded | Converted in 2-60 ms per size; 234x144 cover variant 40 KB, 129x69 contain 2.8 KB, 92x69 contain 21 KB (JPEG quality 85) |
| Asset transfer to the device | 40 KB in 701 ms, 21 KB in 388 ms (about 57 KB/s); the whole push with 3 images and the config took 2.0 s |
| Page redraw with images (`page <n>`) | 165 ms (4x4 page with two GIFs, a PNG, a gauge and a 4x2 now_playing), 188-197 ms (page of small widgets), 146 ms (Now Playing) |
| Internal heap after boot with 6 pages and 3 image widgets | 105 KB free (lowest 102 KB) |
| Firmware | RAM 34.9 % static, flash image about 1.6 MB |

## Display power (2026-11-07)

| Item | Before | After |
|---|---|---|
| Display VCC | 3V3 pin (module regulator U1 starved, about 3.0 V out) | 5Vin, about 4.8 V (`IN-OUT` bridged) |
| Brightness | Dim even at 100 % (gamma 2.2: the 80 % default is 61 % PWM duty) | Clearly brighter at the 80 % default |
| Cold plug-in, native cable first | 1 of 1 white/grey screen until RST | 5 of 5 normal |
| Device health after the change | | Host link, sensors (AHT20 + BMP280, 0 errors), PSRAM all normal |

