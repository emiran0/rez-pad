#include "art_assembler.h"

#include <string.h>

#include "rezpad_protocol.h"

namespace rp {
namespace proto {

namespace {
uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
}  // namespace

ArtAssembler::Result ArtAssembler::begin(uint32_t id, uint32_t size, uint32_t crc32) {
  active_ = false;
  if (size == 0 || size > cap_ || buf_ == nullptr) return Result::kTooLarge;
  id_ = id;
  crc_ = crc32;
  size_ = size;
  received_ = 0;
  active_ = true;
  return Result::kOk;
}

ArtAssembler::Result ArtAssembler::chunk(const uint8_t* body, size_t len) {
  if (len < 8) {
    if (!active_) return Result::kIgnored;
    active_ = false;
    return Result::kBadChunk;
  }
  const uint32_t id = readU32(body);
  const uint32_t offset = readU32(body + 4);
  if (!active_ || id != id_) return Result::kIgnored;
  const size_t n = len - 8;
  if (offset != received_ || n == 0 || received_ + n > size_) {
    active_ = false;
    return Result::kBadChunk;
  }
  memcpy(buf_ + received_, body + 8, n);
  received_ += n;
  return Result::kOk;
}

ArtAssembler::Result ArtAssembler::end(uint32_t id) {
  if (!active_ || id != id_) return Result::kIgnored;
  active_ = false;
  if (received_ != size_) return Result::kIncomplete;
  if (crc32(buf_, size_) != crc_) return Result::kBadCrc;
  return Result::kOk;
}

}  // namespace proto
}  // namespace rp
