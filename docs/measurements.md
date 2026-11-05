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
| | | | | | |

## Encoders

| Encoder | Counts per detent | Direction OK? | Notes |
|---|---|---|---|
| enc1 | | | |
| enc2 | | | |

## Sensor calibration

| Date | Reference temp | AHT20 temp | Offset applied | Reference RH | AHT20 RH | Notes |
|---|---|---|---|---|---|---|
| | | | | | | |
