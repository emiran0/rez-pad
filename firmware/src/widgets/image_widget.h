#pragma once

#include "widget.h"

namespace rp {
namespace widgets {

// image: a still picture or an animation uploaded on the web page. The host converts it to
// this card's exact size (docs/protocol.md, "Assets"); all frames are decoded into PSRAM once
// and an LVGL timer flips through them.
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
  static void onTimer(lv_timer_t* t);
  void showFrame(int i);

  lv_obj_t* img_ = nullptr;
  lv_timer_t* timer_ = nullptr;
  uint16_t* pixels_ = nullptr;  // all frames, w x h RGB565 each
  uint16_t* delays_ = nullptr;
  int frames_ = 0;
  int current_ = 0;
  int w_ = 0, h_ = 0;
  lv_image_dsc_t dsc_ = {};
};

}  // namespace widgets
}  // namespace rp
