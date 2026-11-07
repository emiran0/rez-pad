// Reassembles one album art image from ART_BEGIN / ART_CHUNK / ART_END (docs/protocol.md).
// Chunks must arrive in order (USB bulk transfers are reliable and ordered); anything else aborts
// the transfer. Pure C++, no allocation: the caller owns the buffer. Unit-tested on the host.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace rp {
namespace proto {

class ArtAssembler {
 public:
  enum class Result : uint8_t {
    kOk,          // accepted (chunk stored, or image complete and CRC good on end())
    kIgnored,     // not for the transfer in progress (old id, or none active)
    kBadChunk,    // malformed or out of order: transfer aborted
    kTooLarge,    // begin() size exceeds the buffer
    kIncomplete,  // end() before all bytes arrived: transfer aborted
    kBadCrc,      // end() with all bytes but a CRC mismatch: transfer aborted
  };

  ArtAssembler(uint8_t* buf, size_t capacity) : buf_(buf), cap_(capacity) {}

  // Starts a transfer. Any transfer in progress is dropped (a new image supersedes it).
  Result begin(uint32_t id, uint32_t size, uint32_t crc32);

  // ART_CHUNK body: [id u32 LE][offset u32 LE][data].
  Result chunk(const uint8_t* body, size_t len);

  // ART_END. On kOk the image is data()/size() until the next begin().
  Result end(uint32_t id);

  void abort() { active_ = false; }
  bool active() const { return active_; }
  uint32_t id() const { return id_; }
  const uint8_t* data() const { return buf_; }
  size_t size() const { return size_; }
  size_t received() const { return received_; }

 private:
  uint8_t* buf_;
  size_t cap_;
  bool active_ = false;
  uint32_t id_ = 0;
  uint32_t crc_ = 0;
  size_t size_ = 0;
  size_t received_ = 0;
};

}  // namespace proto
}  // namespace rp
