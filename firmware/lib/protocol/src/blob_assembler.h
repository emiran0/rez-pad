// Reassembles a config file from CONFIG_BEGIN / CONFIG_CHUNK / CONFIG_END (docs/protocol.md), the
// same scheme as album art without the id: chunks [offset u32 LE][data] must arrive in order.
// Pure C++, no allocation: the caller owns the buffer. Unit-tested on the host.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace rp {
namespace proto {

class BlobAssembler {
 public:
  enum class Result : uint8_t {
    kOk,          // accepted (chunk stored, or complete with a good CRC on end())
    kIgnored,     // no transfer in progress
    kBadChunk,    // malformed or out of order: transfer aborted
    kTooLarge,    // begin() size exceeds the buffer (or is 0)
    kIncomplete,  // end() before all bytes arrived: transfer aborted
    kBadCrc,      // end() with all bytes but a CRC mismatch: transfer aborted
  };

  BlobAssembler(uint8_t* buf, size_t capacity) : buf_(buf), cap_(capacity) {}

  // Starts a transfer; any transfer in progress is dropped.
  Result begin(uint32_t size, uint32_t crc32);
  // CONFIG_CHUNK body: [offset u32 LE][data].
  Result chunk(const uint8_t* body, size_t len);
  // CONFIG_END. On kOk the data is data()/size() until the next begin().
  Result end();

  void abort() { active_ = false; }
  bool active() const { return active_; }
  const uint8_t* data() const { return buf_; }
  size_t size() const { return size_; }
  uint32_t crc() const { return crc_; }

 private:
  uint8_t* buf_;
  size_t cap_;
  bool active_ = false;
  uint32_t crc_ = 0;
  size_t size_ = 0;
  size_t received_ = 0;
};

}  // namespace proto
}  // namespace rp
