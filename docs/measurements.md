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
| Track change to text on screen | < 150 ms | | |
| Track change to album art on screen | < 600 ms | | |
| Encoder input to host action executed | < 100 ms | | |
| Full page redraw at 40 MHz SPI | < 120 ms | 92.7 ms (480x40 DMA strips) | 2026-11-07 |
| Device recovers after host restart | < 3 s | | |
| Host recovers after device unplug/replug | < 3 s | | |
| Host average CPU | < 1% | | |
| Host memory | < 150 MB | | |

## Boot log

| Date | Commit | PSRAM size | Flash size | Free heap | Notes |
|---|---|---|---|---|---|
| 2026-11-06 | 7e70ad7 | 8192 KB (8188 KB free) | 16 MB | 347 KB internal | ESP32-S3 (QFN56) rev v0.2, 240 MHz, embedded PSRAM AP_3v3. Flashed over the CH343 bridge at 921600 baud (17.9 s). Protocol self-test ok. Phase 0 check passed. |

## Encoders

| Encoder | Counts per detent | Direction OK? | Notes |
|---|---|---|---|
| enc1 | | | |
| enc2 | | | |

## Sensor calibration

| Date | Reference temp | AHT20 temp | Offset applied | Reference RH | AHT20 RH | Notes |
|---|---|---|---|---|---|---|
| | | | | | | |
