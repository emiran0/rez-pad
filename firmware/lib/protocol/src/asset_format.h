// Image assets for `image` widgets (docs/protocol.md, "Assets"): the file name both sides derive
// from the config, and the RPA1 container (JPEG frames with delays). Pure C++, unit-tested.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace rp {
namespace proto {

constexpr uint32_t kMaxAssetBytes = 1048576;
constexpr size_t kAssetNameMax = 48;  // buffer size for assetName()

// "<id>_<w>x<h>_<c|f>_<rrggbb>.rpa". Returns false when `id` is not 1 to 16 lowercase hex or
// letters/digits only, or the buffer is too small.
bool assetName(char* out, size_t n, const char* id, int w, int h, bool contain, uint32_t bg);

struct RpaFrame {
  uint16_t delayMs;
  const uint8_t* jpeg;
  uint32_t len;
};

// Reads an RPA1 file held in memory.
class RpaReader {
 public:
  // False for a bad magic, a truncated table or zero frames.
  bool open(const uint8_t* data, size_t len);
  uint16_t width() const { return w_; }
  uint16_t height() const { return h_; }
  uint16_t frames() const { return frames_; }
  // Frames in order; false after the last one or on a truncated frame.
  bool next(RpaFrame& f);

 private:
  const uint8_t* data_ = nullptr;
  size_t len_ = 0;
  size_t pos_ = 0;
  uint16_t w_ = 0, h_ = 0, frames_ = 0, read_ = 0;
};

}  // namespace proto
}  // namespace rp
