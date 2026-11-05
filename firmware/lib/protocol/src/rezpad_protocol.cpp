#include "rezpad_protocol.h"

namespace rp {
namespace proto {

uint16_t crc16(const uint8_t* data, size_t len, uint16_t crc) {
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc) {
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
  }
  return ~crc;
}

namespace {

// Streaming canonical COBS encoder writing into a caller buffer.
class CobsWriter {
 public:
  CobsWriter(uint8_t* out, size_t cap) : out_(out), cap_(cap) { open(); }

  void put(uint8_t b) {
    if (b == 0) {
      // A full 0xFF block does not imply a zero, so after one the zero needs its own (empty) block.
      if (closedFull_) open();
      close();
      open();
      return;
    }
    if (closedFull_) open();  // a full block just ended and more data follows
    write(b);
    if (++code_ == 0xFF) {
      close();
      closedFull_ = true;
    }
  }

  // Returns the encoded length, or 0 on overflow.
  size_t finish() {
    if (!closedFull_) close();
    return ok_ ? pos_ : 0;
  }

 private:
  void open() {
    codeIdx_ = pos_;
    write(0);  // placeholder for the code byte
    code_ = 1;
    closedFull_ = false;
  }
  void close() {
    if (ok_) out_[codeIdx_] = code_;
  }
  void write(uint8_t b) {
    if (pos_ >= cap_) {
      ok_ = false;
      return;
    }
    out_[pos_++] = b;
  }

  uint8_t* out_;
  size_t cap_;
  size_t pos_ = 0;
  size_t codeIdx_ = 0;
  uint8_t code_ = 1;
  bool closedFull_ = false;
  bool ok_ = true;
};

}  // namespace

size_t cobsEncode(const uint8_t* in, size_t len, uint8_t* out, size_t outCap) {
  CobsWriter w(out, outCap);
  for (size_t i = 0; i < len; ++i) w.put(in[i]);
  return w.finish();
}

int32_t cobsDecode(const uint8_t* in, size_t len, uint8_t* out, size_t outCap) {
  size_t i = 0;
  size_t o = 0;
  while (i < len) {
    const uint8_t code = in[i++];
    if (code == 0) return -1;
    const size_t n = static_cast<size_t>(code) - 1;
    if (i + n > len || o + n > outCap) return -1;
    for (size_t k = 0; k < n; ++k) {
      if (in[i] == 0) return -1;
      out[o++] = in[i++];
    }
    if (code != 0xFF && i < len) {
      if (o >= outCap) return -1;
      out[o++] = 0;
    }
  }
  return static_cast<int32_t>(o);
}

size_t buildFrame(uint8_t type, uint16_t seq, const uint8_t* body, size_t bodyLen,
                  uint8_t* out, size_t outCap) {
  if (bodyLen > kMaxBody || outCap == 0) return 0;
  const uint8_t header[kHeaderSize] = {kVersion, type, static_cast<uint8_t>(seq & 0xFF),
                                       static_cast<uint8_t>(seq >> 8)};
  uint16_t crc = crc16(header, kHeaderSize);
  crc = crc16(body, bodyLen, crc);

  CobsWriter w(out, outCap - 1);  // reserve room for the 0x00 delimiter
  for (size_t i = 0; i < kHeaderSize; ++i) w.put(header[i]);
  for (size_t i = 0; i < bodyLen; ++i) w.put(body[i]);
  w.put(static_cast<uint8_t>(crc & 0xFF));
  w.put(static_cast<uint8_t>(crc >> 8));
  const size_t n = w.finish();
  if (n == 0) return 0;
  out[n] = 0;
  return n + 1;
}

ParseResult parsePacket(const uint8_t* pkt, size_t len, Packet& out) {
  if (len < kMinPacket) return ParseResult::kTooShort;
  if (len > kMaxPacket) return ParseResult::kTooLarge;
  if (pkt[0] != kVersion) return ParseResult::kBadVersion;
  const uint16_t expected = static_cast<uint16_t>(pkt[len - 2] | (pkt[len - 1] << 8));
  if (crc16(pkt, len - kCrcSize) != expected) return ParseResult::kBadCrc;
  out.type = pkt[1];
  out.seq = static_cast<uint16_t>(pkt[2] | (pkt[3] << 8));
  out.body = pkt + kHeaderSize;
  out.bodyLen = len - kMinPacket;
  return ParseResult::kOk;
}

bool FrameDecoder::push(uint8_t byte) {
  if (byte != 0) {
    if (len_ < kBufSize) {
      buf_[len_++] = byte;
    } else {
      overflow_ = true;
    }
    return false;
  }

  // End of frame.
  const size_t encodedLen = len_;
  const bool overflowed = overflow_;
  reset();
  if (encodedLen == 0) return false;  // back-to-back delimiters, used for resync
  if (overflowed) {
    ++stats_.tooLarge;
    return false;
  }
  const int32_t n = cobsDecode(buf_, encodedLen, buf_, kBufSize);
  if (n < 0) {
    ++stats_.badCobs;
    return false;
  }
  switch (parsePacket(buf_, static_cast<size_t>(n), packet_)) {
    case ParseResult::kOk:
      ++stats_.ok;
      return true;
    case ParseResult::kTooShort: ++stats_.tooShort; break;
    case ParseResult::kTooLarge: ++stats_.tooLarge; break;
    case ParseResult::kBadVersion: ++stats_.badVersion; break;
    case ParseResult::kBadCrc: ++stats_.badCrc; break;
  }
  return false;
}

}  // namespace proto
}  // namespace rp
