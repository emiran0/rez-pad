"""Generate docs/protocol-test-vectors.json.

Reference implementation of rez-pad protocol v1 framing (COBS, CRC16, CRC32,
packet layout). Host (C#) and firmware (C++) unit tests both load the output
file, so the two sides cannot drift apart.

Usage: python tools/gen_protocol_vectors.py
"""

import json
import pathlib
import zlib

PROTOCOL_VERSION = 1


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def cobs_encode(data: bytes) -> bytes:
    """Canonical COBS: no trailing 0x01 after a full 0xFF block that ends the input."""
    out = bytearray()
    block = bytearray()
    closed_full = False
    for b in data:
        if b == 0:
            out.append(len(block) + 1)
            out += block
            block.clear()
            closed_full = False
        else:
            block.append(b)
            closed_full = False
            if len(block) == 254:
                out.append(0xFF)
                out += block
                block.clear()
                closed_full = True
    if not (closed_full and not block):
        out.append(len(block) + 1)
        out += block
    return bytes(out)


def cobs_decode(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data):
        code = data[i]
        if code == 0:
            raise ValueError("zero in COBS data")
        i += 1
        end = i + code - 1
        if end > len(data):
            raise ValueError("truncated COBS block")
        out += data[i:end]
        i = end
        if code < 0xFF and i < len(data):
            out.append(0)
    return bytes(out)


def build_frame(msg_type: int, seq: int, body: bytes) -> bytes:
    pkt = bytes([PROTOCOL_VERSION, msg_type]) + seq.to_bytes(2, "little") + body
    pkt += crc16_ccitt_false(pkt).to_bytes(2, "little")
    return cobs_encode(pkt) + b"\x00"


def hx(b: bytes) -> str:
    return b.hex()


def main() -> None:
    check = b"123456789"

    cobs_inputs = [
        b"",
        b"\x00",
        b"\x00\x00",
        b"\x11\x22\x00\x33",
        b"\x11\x22\x33\x44",
        b"\x11\x00\x00\x00",
        bytes(range(1, 255)),        # 254 non-zero bytes
        bytes(range(0, 255)),        # 0x00..0xFE
        bytes(range(1, 256)),        # 0x01..0xFF (255 non-zero bytes)
        bytes(range(2, 256)) + b"\x00",
        bytes(range(3, 256)) + b"\x00\x01",
    ]
    cobs = []
    for raw in cobs_inputs:
        enc = cobs_encode(raw)
        assert cobs_decode(enc) == raw
        assert 0 not in enc
        cobs.append({"decoded": hx(raw), "encoded": hx(enc)})

    # Known values from the COBS paper / Wikipedia, as a sanity check of the reference.
    assert cobs_encode(b"\x00") == b"\x01\x01"
    assert cobs_encode(b"\x11\x22\x00\x33") == b"\x03\x11\x22\x02\x33"
    assert cobs_encode(bytes(range(1, 255))) == b"\xff" + bytes(range(1, 255))
    assert cobs_encode(bytes(range(1, 256))) == b"\xff" + bytes(range(1, 255)) + b"\x02\xff"
    assert crc16_ccitt_false(check) == 0x29B1
    assert crc32(check) == 0xCBF43926

    frames = []
    for name, t, seq, body in [
        ("PING", 0x02, 0, b""),
        ("PONG", 0x82, 1, b""),
        ("HELLO", 0x01, 513, b'{"proto":1,"host_ver":"0.1.0"}'),
        ("ART_CHUNK", 0x12, 65535, (7).to_bytes(4, "little") + (4096).to_bytes(4, "little") + b"\x00\xff\x00\x10"),
        ("CMD_unicode", 0x60, 42, '{"cmd":"identify","args":{"name":"Çağı"}}'.encode("utf-8")),
    ]:
        frames.append({
            "name": name,
            "type": t,
            "seq": seq,
            "body": hx(body),
            "frame": hx(build_frame(t, seq, body)),
        })

    bad_frames = [
        {"name": "bad_crc", "frame": hx(build_frame(0x02, 0, b"")[:-2] + bytes([build_frame(0x02, 0, b"")[-2] ^ 0x01]) + b"\x00")},
        {"name": "too_short", "frame": hx(cobs_encode(b"\x01\x02\x00") + b"\x00")},
        {"name": "bad_version", "frame": hx(cobs_encode(b"\x02\x02\x00\x00" + crc16_ccitt_false(b"\x02\x02\x00\x00").to_bytes(2, "little")) + b"\x00")},
        {"name": "bad_cobs", "frame": hx(b"\x05\x11\x22\x00")},
    ]

    out = {
        "protocol_version": PROTOCOL_VERSION,
        "crc16": [
            {"data": hx(check), "crc": crc16_ccitt_false(check)},
            {"data": "", "crc": crc16_ccitt_false(b"")},
            {"data": hx(bytes(range(256))), "crc": crc16_ccitt_false(bytes(range(256)))},
        ],
        "crc32": [
            {"data": hx(check), "crc": crc32(check)},
            {"data": "", "crc": crc32(b"")},
            {"data": hx(bytes(range(256))), "crc": crc32(bytes(range(256)))},
        ],
        "cobs": cobs,
        "frames": frames,
        "bad_frames": bad_frames,
    }

    root = pathlib.Path(__file__).resolve().parent.parent
    path = root / "docs" / "protocol-test-vectors.json"
    path.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {path}")

    hpath = root / "firmware" / "test" / "test_protocol" / "vectors.h"
    hpath.write_text(c_header(out), encoding="utf-8", newline="\n")
    print(f"wrote {hpath}")


def c_bytes(hexstr: str) -> str:
    if not hexstr:
        return "{0}"
    b = bytes.fromhex(hexstr)
    return "{" + ",".join(f"0x{x:02x}" for x in b) + "}"


def c_header(v: dict) -> str:
    lines = [
        "// GENERATED by tools/gen_protocol_vectors.py from the same data as",
        "// docs/protocol-test-vectors.json. Do not edit by hand.",
        "#pragma once",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "struct CrcVector { const uint8_t* data; size_t len; uint32_t crc; };",
        "struct CobsVector { const uint8_t* dec; size_t decLen; const uint8_t* enc; size_t encLen; };",
        "struct FrameVector { const char* name; uint8_t type; uint16_t seq;",
        "                     const uint8_t* body; size_t bodyLen; const uint8_t* frame; size_t frameLen; };",
        "struct BadFrameVector { const char* name; const uint8_t* frame; size_t frameLen; };",
        "",
    ]

    def arr(name: str, hexstr: str) -> str:
        n = len(hexstr) // 2
        lines.append(f"static const uint8_t {name}[] = {c_bytes(hexstr)};")
        return f"{name}, {n}"

    for kind in ("crc16", "crc32"):
        refs = [f"{{{arr(f'k_{kind}_{i}', e['data'])}, 0x{e['crc']:x}u}}" for i, e in enumerate(v[kind])]
        lines.append(f"static const CrcVector k{kind.upper()}[] = {{{', '.join(refs)}}};")
    refs = [f"{{{arr(f'k_cobs_dec_{i}', e['decoded'])}, {arr(f'k_cobs_enc_{i}', e['encoded'])}}}" for i, e in enumerate(v["cobs"])]
    lines.append(f"static const CobsVector kCOBS[] = {{{', '.join(refs)}}};")
    refs = []
    for i, e in enumerate(v["frames"]):
        refs.append(f"{{\"{e['name']}\", 0x{e['type']:02x}, {e['seq']}, {arr(f'k_fr_body_{i}', e['body'])}, {arr(f'k_fr_frame_{i}', e['frame'])}}}")
    lines.append(f"static const FrameVector kFRAMES[] = {{{', '.join(refs)}}};")
    refs = [f"{{\"{e['name']}\", {arr(f'k_bad_{i}', e['frame'])}}}" for i, e in enumerate(v["bad_frames"])]
    lines.append(f"static const BadFrameVector kBAD_FRAMES[] = {{{', '.join(refs)}}};")
    lines.append("")
    return "\n".join(lines)


if __name__ == "__main__":
    main()
