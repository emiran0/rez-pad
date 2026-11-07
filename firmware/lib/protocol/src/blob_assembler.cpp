#include "blob_assembler.h"

#include <string.h>

#include "rezpad_protocol.h"

namespace rp {
namespace proto {

BlobAssembler::Result BlobAssembler::begin(uint32_t size, uint32_t crc32) {
  active_ = false;
  if (size == 0 || size > cap_ || buf_ == nullptr) return Result::kTooLarge;
  crc_ = crc32;
  size_ = size;
  received_ = 0;
  active_ = true;
  return Result::kOk;
}

BlobAssembler::Result BlobAssembler::chunk(const uint8_t* body, size_t len) {
  if (!active_) return Result::kIgnored;
  if (len < 5) {
    active_ = false;
    return Result::kBadChunk;
  }
  const uint32_t offset = static_cast<uint32_t>(body[0]) | (static_cast<uint32_t>(body[1]) << 8) |
                          (static_cast<uint32_t>(body[2]) << 16) | (static_cast<uint32_t>(body[3]) << 24);
  const size_t n = len - 4;
  if (offset != received_ || received_ + n > size_) {
    active_ = false;
    return Result::kBadChunk;
  }
  memcpy(buf_ + received_, body + 4, n);
  received_ += n;
  return Result::kOk;
}

BlobAssembler::Result BlobAssembler::end() {
  if (!active_) return Result::kIgnored;
  active_ = false;
  if (received_ != size_) return Result::kIncomplete;
  if (crc32(buf_, size_) != crc_) return Result::kBadCrc;
  return Result::kOk;
}

}  // namespace proto
}  // namespace rp
