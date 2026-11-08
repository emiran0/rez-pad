#include "asset_format.h"

#include <stdio.h>
#include <string.h>

namespace rp {
namespace proto {

namespace {

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool assetName(char* out, size_t n, const char* id, int w, int h, bool contain, uint32_t bg) {
  const size_t idLen = id ? strlen(id) : 0;
  if (idLen == 0 || idLen > 16) return false;
  for (size_t i = 0; i < idLen; ++i) {
    const char c = id[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'))) return false;
  }
  const int len = snprintf(out, n, "%s_%dx%d_%c_%06lx.rpa", id, w, h, contain ? 'f' : 'c',
                           static_cast<unsigned long>(bg & 0xFFFFFF));
  return len > 0 && static_cast<size_t>(len) < n;
}

bool RpaReader::open(const uint8_t* data, size_t len) {
  data_ = data;
  len_ = len;
  pos_ = 12;
  read_ = 0;
  if (!data || len < 12 || memcmp(data, "RPA1", 4) != 0) return false;
  w_ = u16(data + 4);
  h_ = u16(data + 6);
  frames_ = u16(data + 8);
  return w_ > 0 && h_ > 0 && frames_ > 0;
}

bool RpaReader::next(RpaFrame& f) {
  if (read_ >= frames_ || pos_ + 6 > len_) return false;
  f.delayMs = u16(data_ + pos_);
  f.len = u32(data_ + pos_ + 2);
  pos_ += 6;
  if (f.len == 0 || f.len > len_ - pos_) return false;
  f.jpeg = data_ + pos_;
  pos_ += f.len;
  ++read_;
  return true;
}

}  // namespace proto
}  // namespace rp
