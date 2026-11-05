# Measurements

Record real numbers from the hardware here. Include date, firmware commit and conditions.

## SPI clock (ILI9488)

| Date | Commit | SPI Hz | Full-screen fill (ms) | 200x200 region (ms) | Artifacts? | Notes |
|---|---|---|---|---|---|---|
| | | 40 MHz | | | | |
| | | 60 MHz | | | | |
| | | 80 MHz | | | | |

Chosen default: _TBD_ (Phase 1).

## Performance targets

| Metric | Target | Measured | Date |
|---|---|---|---|
| Track change to text on screen | < 150 ms | | |
| Track change to album art on screen | < 600 ms | | |
| Encoder input to host action executed | < 100 ms | | |
| Full page redraw at 40 MHz SPI | < 120 ms | | |
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
