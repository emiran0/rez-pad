# USB protocol (v1)

`PROTOCOL_VERSION = 1`. Implemented in:

- Host: `host/src/RezPad.Protocol/`
- Firmware: `firmware/lib/protocol/` (pure C++, no Arduino deps, unit-tested on the `native` PlatformIO env)

Both implementations are tested against the same vectors in [`protocol-test-vectors.json`](protocol-test-vectors.json). Any change to framing must update that file.

Any breaking change bumps `PROTOCOL_VERSION`. Both sides report their version in the handshake and refuse incompatible peers with a clear message.

## USB identity and discovery

The native USB port is a composite device (TinyUSB):

| Item | Value |
|---|---|
| VID / PID | `0x303A` / `0x4005` (Espressif VID; TinyUSB's pre-allocated PID for CDC + HID) |
| Manufacturer / product | `rez-pad` / `rez-pad` (the composite device's bus-reported description) |
| Serial number | `RP-` + the chip's 12 hex digit MAC, e.g. `RP-ACA704F47EDC` (stable per board) |
| Interface 0 | HID consumer control (media keys, offline fallback) |
| Interface 1 | CDC ACM, the protocol link (`USB\VID_303A&PID_4005&MI_01\...`, a normal COM port) |

The host finds the COM port whose parent composite device has VID/PID 303A:4005 and product `rez-pad` (other TinyUSB CDC+HID devices share that PID).

The host opens the port with DTR and RTS on (TinyUSB only sends while DTR is set) at any baud rate (ignored). The firmware does **not** reboot into the bootloader on esptool's DTR/RTS sequence or a 1200 baud "touch": a normal open/close from .NET produced part of that sequence, so it was disabled. Flash through the USB-UART port, or hold BOOT and tap RESET to flash through the native port.

## Framing

- Transport: USB CDC on the native USB port (baud setting is ignored). USB bulk transfers are reliable and flow-controlled, so there are no per-chunk ACKs.
- Each frame on the wire: `COBS(packet) + 0x00`.
- `packet` = `[ver u8 = 1][type u8][seq u16 LE][body ...][crc16 u16 LE]`
- **CRC16**: CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, xorout 0x0000) over `ver..body`. Check value for ASCII `123456789` is `0x29B1`.
- **CRC32** (art, config): CRC-32/ISO-HDLC (the zlib/Ethernet CRC, poly 0x04C11DB7 reflected, init 0xFFFFFFFF, xorout 0xFFFFFFFF). Check value `0xCBF43926`.
- `seq`: each side keeps its own counter, starting at 0, incremented per sent packet, wrapping at 65535. Used for logs and drop statistics only, never for ordering.
- Max decoded packet size: **8192 bytes** (header + body + CRC). Larger data (art, config) is chunked. Recommended chunk payload: 4096 bytes.
- All multi-byte integers in binary bodies are little endian.
- Body is either UTF-8 JSON (small structured messages) or a binary layout (bulk data). Each type below says which. An empty body is zero bytes (not `{}`).
- Receivers drop and count: frames with bad COBS, packets shorter than 6 bytes, bad CRC, oversized packets, unknown `ver`. Unknown `type` is counted and ignored. Nothing ever crashes or resets the link.

## Host -> Device

| Type | Name | Body | Notes |
|---|---|---|---|
| 0x01 | HELLO | JSON `{proto, host_ver}` | On connect |
| 0x02 | PING | empty | Every 1 s |
| 0x03 | TIME_SYNC | JSON `{unix_ms, tz_offset_min}` | On connect, then every 60 s |
| 0x10 | NOW_PLAYING | JSON, see below | On change, plus resync every 5 s |
| 0x11 | ART_BEGIN | JSON `{id, w, h, fmt:"jpeg", size, crc32}` | Any ART_BEGIN (same or new `id`) drops a transfer in progress |
| 0x12 | ART_CHUNK | binary `[id u32][offset u32][data]` | |
| 0x13 | ART_END | JSON `{id}` | Device verifies CRC32, decodes, swaps image |
| 0x14 | ART_NONE | JSON `{id}` | Track has no art, show placeholder |
| 0x15 | VOLUME | JSON `{level, muted}` | level 0 to 100 |
| 0x20 | SYS_STATS | JSON `{t, v:{"<source_id>": number or null, ...}}` | Only referenced sources, after every sample (default 1 Hz) |
| 0x30 | WEATHER | JSON, see below | Every 15 min and on connect |
| 0x40 | CONFIG_BEGIN | JSON `{size, crc32}` | |
| 0x41 | CONFIG_CHUNK | binary `[offset u32][data]` | |
| 0x42 | CONFIG_END | empty | Device validates, saves, applies, replies CONFIG_ACK |
| 0x43 | CONFIG_GET | empty | Device replies with its config (chunked, same scheme reversed) |
| 0x50 | SPECTRUM | binary `[n u8][bins u8 * n]` | Stretch goal, ~30 Hz |
| 0x60 | CMD | JSON `{cmd, args}` | Debug/maintenance: `set_spi_hz`, `reboot`, `factory_reset`, `bench_display`, `identify` |

### NOW_PLAYING body

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

- `state` is one of `playing`, `paused`, `stopped`, `none`.
- `track_id` is a 32-bit hash (CRC32 of UTF-8 `title + "\n" + artist + "\n" + album`).
- `ts` is the host unix time in ms when `pos_ms` was sampled. The device interpolates position locally from `pos_ms`, `ts` and its synced clock (an age above 10 s is treated as clock skew and clamped).
- Text is raw UTF-8 in the JSON (not `\uXXXX` escapes). The device cuts fields that do not fit its buffers (title 127 bytes, artist and album 95, app 31) at a character boundary.
- The host sends NOW_PLAYING when the track, state or controls change, when the position jumps by more than 1.5 s (seek), and every 5 s.

### Album art

- `id` is the `track_id` the image belongs to. The host sends a square baseline JPEG (quality 85) of the size from HELLO_ACK `art` (config `media.art_size`, default 200, when the device does not say).
- ART_CHUNK offsets must be contiguous from 0; anything else drops the transfer. ART_END checks size and CRC32, then the device decodes into RGB565 and shows it once `id` matches the current track.
- ART_NONE: the track has no art (or it could not be converted); the device shows its placeholder.
- The host skips art the device already has (same `id` and JPEG CRC) and sends it again after every (re)connect.
- ART_REQUEST: the device sends it once per track when the current track still has no art 2.5 s after it started showing (lost transfer, bad CRC). The host answers with the art or ART_NONE.

### SYS_STATS body

```json
{ "t": 1793912345678, "v": { "sys.cpu.load": 12.5, "sys.cpu.temp": null, "sys.gpu.temp": 49 } }
```

- `t` is the host unix time in ms of the sample. Values are rounded to one decimal.
- `null` means the layout uses the source but this PC has no value for it right now (no PawnIO driver or no admin rights for CPU temperature, no such GPU). The device shows `--`.
- Each message carries the complete set; a source missing from it also shows `--`. The device keeps up to 16 sources with ids shorter than 32 bytes and drops (counts) the rest.
- When the host is lost the device clears all values.

### WEATHER body

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

## Device -> Host

| Type | Name | Body | Notes |
|---|---|---|---|
| 0x81 | HELLO_ACK | JSON `{proto, fw_ver, w, h, psram_free, cfg_crc, buttons, serial, art}` | Reply to HELLO. `serial` is the USB serial number; `art` is the album art edge in px the device's layout wants (omitted when the layout has no `album_art`). Both added within v1, optional for readers |
| 0x82 | PONG | empty | Reply to PING |
| 0x90 | ACTION | JSON `{action, value}` | Resolved from input map, see `config-schema.md` |
| 0x91 | ART_REQUEST | JSON `{id}` | Device lost or never received art for current track |
| 0xA0 | SENSOR | JSON `{temp_c, hum_pct, press_hpa, trend}` | Every 10 s |
| 0xC0 | CONFIG_ACK | JSON `{ok, crc32, error}` | |
| 0xC1 | CONFIG_DATA_BEGIN | JSON `{size, crc32}` | Reply to CONFIG_GET |
| 0xC2 | CONFIG_DATA_CHUNK | binary `[offset u32][data]` | |
| 0xC3 | CONFIG_DATA_END | empty | |
| 0xE0 | LOG | JSON `{lvl, msg}` | Mirrors important device logs to the host |
| 0xE1 | ERROR | JSON `{code, msg}` | Codes: `proto_mismatch` (HELLO with another version), `need_hello` (PINGs without a HELLO, e.g. after a device reset; the host re-sends HELLO), `bad_json` |
| 0xE2 | CMD_RESULT | JSON `{cmd, ok, data}` | |

## Connection rules

- Host considers the device lost after 3 s without PONG. Device considers the host lost after 3 s without PING.
- On (re)connect: HELLO -> HELLO_ACK -> TIME_SYNC -> config check (push if CRC differs) -> NOW_PLAYING -> art -> VOLUME -> WEATHER -> SYS_STATS stream.
- The device replies to every PING with PONG, but only counts the host as connected after a HELLO. If it keeps getting PINGs without a HELLO for 1 s it sends ERROR `need_hello` once.
- Before HELLO_ACK arrives, the host repeats HELLO every 500 ms (the device may still be booting).
- TIME_SYNC sets the device's system clock (UTC) and keeps `tz_offset_min` for local time; the host re-sends it every 60 s, which also covers DST changes.
- CMD implemented so far: `reboot` (replies CMD_RESULT ok, then restarts). Others reply `ok: false`.
- If HELLO_ACK reports a different `proto`, the host shows "Firmware protocol vN, host expects v1: update firmware/host" in the tray and web UI and stops sending anything except HELLO.
