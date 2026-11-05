# USB protocol (v1)

`PROTOCOL_VERSION = 1`. Implemented in:

- Host: `host/src/DeskHub.Protocol/`
- Firmware: `firmware/lib/protocol/` (pure C++, no Arduino deps, unit-tested on the `native` PlatformIO env)

Both implementations are tested against the same vectors in [`protocol-test-vectors.json`](protocol-test-vectors.json). Any change to framing must update that file.

Any breaking change bumps `PROTOCOL_VERSION`. Both sides report their version in the handshake and refuse incompatible peers with a clear message.

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
| 0x11 | ART_BEGIN | JSON `{id, w, h, fmt:"jpeg", size, crc32}` | New `id` aborts any transfer in progress |
| 0x12 | ART_CHUNK | binary `[id u32][offset u32][data]` | |
| 0x13 | ART_END | JSON `{id}` | Device verifies CRC32, decodes, swaps image |
| 0x14 | ART_NONE | JSON `{id}` | Track has no art, show placeholder |
| 0x15 | VOLUME | JSON `{level, muted}` | level 0 to 100 |
| 0x20 | SYS_STATS | JSON `{t, v:{"<source_id>": number, ...}}` | Only referenced sources |
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
- `ts` is the host unix time in ms when `pos_ms` was sampled. The device interpolates position locally from `pos_ms`, `ts` and its synced clock.

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
| 0x81 | HELLO_ACK | JSON `{proto, fw_ver, w, h, psram_free, cfg_crc, buttons}` | Reply to HELLO |
| 0x82 | PONG | empty | Reply to PING |
| 0x90 | ACTION | JSON `{action, value}` | Resolved from input map, see `config-schema.md` |
| 0x91 | ART_REQUEST | JSON `{id}` | Device lost or never received art for current track |
| 0xA0 | SENSOR | JSON `{temp_c, hum_pct, press_hpa, trend}` | Every 10 s |
| 0xC0 | CONFIG_ACK | JSON `{ok, crc32, error}` | |
| 0xC1 | CONFIG_DATA_BEGIN | JSON `{size, crc32}` | Reply to CONFIG_GET |
| 0xC2 | CONFIG_DATA_CHUNK | binary `[offset u32][data]` | |
| 0xC3 | CONFIG_DATA_END | empty | |
| 0xE0 | LOG | JSON `{lvl, msg}` | Mirrors important device logs to the host |
| 0xE1 | ERROR | JSON `{code, msg}` | |
| 0xE2 | CMD_RESULT | JSON `{cmd, ok, data}` | |

## Connection rules

- Host considers the device lost after 3 s without PONG. Device considers the host lost after 3 s without PING.
- On (re)connect: HELLO -> HELLO_ACK -> TIME_SYNC -> config check (push if CRC differs) -> NOW_PLAYING -> art -> VOLUME -> WEATHER -> SYS_STATS stream.
- If HELLO_ACK reports a different `proto`, the host shows "Firmware protocol vN, host expects v1: update firmware/host" in the tray and web UI and stops sending anything except HELLO.
