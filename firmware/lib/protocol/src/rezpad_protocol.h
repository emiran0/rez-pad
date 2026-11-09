// rez-pad USB protocol v1: framing primitives.
// Spec: docs/protocol.md. Shared test vectors: docs/protocol-test-vectors.json.
//
// Pure C++17, no Arduino or ESP-IDF dependencies, no heap allocation, so it
// can be unit-tested on the host (PlatformIO `native` env).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace rp {
namespace proto {

constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderSize = 4;  // ver, type, seq u16
constexpr size_t kCrcSize = 2;
constexpr size_t kMinPacket = kHeaderSize + kCrcSize;
constexpr size_t kMaxPacket = 8192;  // decoded packet, header + body + crc
constexpr size_t kMaxBody = kMaxPacket - kMinPacket;
constexpr size_t kChunkSize = 4096;  // recommended ART/CONFIG chunk payload

constexpr size_t cobsMaxEncodedSize(size_t n) { return n + n / 254 + 1; }
// Worst-case wire size of a frame (COBS + trailing 0x00) for a body of n bytes.
constexpr size_t maxFrameSize(size_t bodyLen) { return cobsMaxEncodedSize(bodyLen + kMinPacket) + 1; }

enum MsgType : uint8_t {
  // Host -> Device
  kHello = 0x01,
  kPing = 0x02,
  kTimeSync = 0x03,
  kNowPlaying = 0x10,
  kArtBegin = 0x11,
  kArtChunk = 0x12,
  kArtEnd = 0x13,
  kArtNone = 0x14,
  kVolume = 0x15,
  kSysStats = 0x20,
  kWeather = 0x30,
  kConfigBegin = 0x40,
  kConfigChunk = 0x41,
  kConfigEnd = 0x42,
  kConfigGet = 0x43,
  kAssetBegin = 0x44,
  kAssetChunk = 0x45,
  kAssetEnd = 0x46,
  kAssetList = 0x47,
  kMixer = 0x16,
  kSpectrum = 0x50,
  kCmd = 0x60,
  // Device -> Host
  kHelloAck = 0x81,
  kPong = 0x82,
  kAction = 0x90,
  kArtRequest = 0x91,
  kInputLearned = 0x92,
  kSensor = 0xA0,
  kConfigAck = 0xC0,
  kConfigDataBegin = 0xC1,
  kConfigDataChunk = 0xC2,
  kConfigDataEnd = 0xC3,
  kAssetAck = 0xC4,
  kAssetListData = 0xC5,
  kLog = 0xE0,
  kError = 0xE1,
  kCmdResult = 0xE2,
};

// CRC-16/CCITT-FALSE. Pass the previous result as `crc` to continue a running CRC.
uint16_t crc16(const uint8_t* data, size_t len, uint16_t crc = 0xFFFF);

// CRC-32/ISO-HDLC (zlib). Pass the previous result as `crc` to continue a running CRC.
uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0);

// Encodes `len` bytes. Returns the encoded length, or 0 if `outCap` is too small
// (valid output is never empty: encoding zero bytes yields one byte).
size_t cobsEncode(const uint8_t* in, size_t len, uint8_t* out, size_t outCap);

// Decodes one COBS block (without the trailing 0x00). `out` may equal `in`
// (in-place decoding is safe). Returns the decoded length, or -1 on malformed
// input or insufficient capacity.
int32_t cobsDecode(const uint8_t* in, size_t len, uint8_t* out, size_t outCap);

// Builds a complete wire frame (COBS(packet) + 0x00). Returns the frame length,
// or 0 if the body is too large or `outCap` is too small.
size_t buildFrame(uint8_t type, uint16_t seq, const uint8_t* body, size_t bodyLen,
                  uint8_t* out, size_t outCap);

struct Packet {
  uint8_t type = 0;
  uint16_t seq = 0;
  const uint8_t* body = nullptr;
  size_t bodyLen = 0;
};

enum class ParseResult : uint8_t { kOk, kTooShort, kTooLarge, kBadVersion, kBadCrc };

// Validates a decoded packet (after COBS). On kOk, `out.body` points into `pkt`.
ParseResult parsePacket(const uint8_t* pkt, size_t len, Packet& out);

struct DecoderStats {
  uint32_t ok = 0;
  uint32_t badCobs = 0;
  uint32_t tooShort = 0;
  uint32_t tooLarge = 0;  // includes frames that overflowed the receive buffer
  uint32_t badVersion = 0;
  uint32_t badCrc = 0;
};

// Streaming receiver: feed bytes as they arrive. Malformed frames are dropped
// and counted, never fatal. Uses a fixed internal buffer (~8.2 KB).
class FrameDecoder {
 public:
  // Returns true when a complete valid packet is available via packet().
  // The packet stays valid until the next call to push().
  bool push(uint8_t byte);
  const Packet& packet() const { return packet_; }
  const DecoderStats& stats() const { return stats_; }
  void reset() { len_ = 0; overflow_ = false; }

 private:
  static constexpr size_t kBufSize = cobsMaxEncodedSize(kMaxPacket);
  uint8_t buf_[kBufSize];
  size_t len_ = 0;
  bool overflow_ = false;
  Packet packet_;
  DecoderStats stats_;
};

}  // namespace proto
}  // namespace rp
