#pragma once

#include "asset_format.h"
#include "widget.h"

namespace rp {
namespace widgets {

// image: a still picture or an animation uploaded on the web page. The host converts it to
// this card's exact size (docs/protocol.md, "Assets"). An LVGL timer flips through the frames:
// decoded into PSRAM once when they fit (kPreDecodeWidget per widget, kPreDecodeTotal in all),
// otherwise the JPEG frames stay in PSRAM and each is decoded just before it is shown.
class ImageWidget : public Widget {
 public:
  using Widget::Widget;
  ~ImageWidget() override;
  bool fullBleed() const override { return true; }
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override { return 0; }
  void refresh(const ui::Model&, uint32_t, uint32_t) override {}

  // Image widgets whose file was missing when they were built (the UI rebuilds when files arrive).
  static int missing();
  static void resetMissing();

 private:
  static constexpr size_t kPreDecodeWidget = 2u * 1024 * 1024;
  static constexpr size_t kPreDecodeTotal = 4u * 1024 * 1024;

  static void onTimer(lv_timer_t* t);
  void showFrame(int i);
  void showPixels(const uint16_t* px);

  lv_obj_t* img_ = nullptr;
  lv_timer_t* timer_ = nullptr;
  uint16_t* pixels_ = nullptr;  // pre-decoded: all frames; streaming: two frame buffers
  uint16_t* delays_ = nullptr;
  size_t preDecoded_ = 0;       // bytes counted against kPreDecodeTotal
  // Streaming: the RPA file and its frames (pointers into it); back_ is the buffer to decode into.
  uint8_t* file_ = nullptr;
  proto::RpaFrame* jpegs_ = nullptr;
  int back_ = 0;
  int frames_ = 0;
  int current_ = 0;
  int w_ = 0, h_ = 0;
  lv_image_dsc_t dsc_ = {};
};

}  // namespace widgets
}  // namespace rp
