#pragma once

#include "widget.h"

namespace rp {
namespace widgets {

// Audio spectrum bars (SPECTRUM from the host). `bars` 4 to 32, or one per 10 px of width.
// The host's 32 bins are grouped into the bars (loudest bin of each group).
//
// Drawn by one object, not one LVGL object per bar: LVGL keeps at most 32 dirty areas and redraws
// the whole screen when they run out, which moving 32 bar objects did on every frame (5 FPS).
// Here each frame marks only the strips where bar tops moved, merged into at most kDirtyGroups areas.
class SpectrumWidget : public Widget {
 public:
  static constexpr int kMaxBars = ui::kSpectrumBins;
  static constexpr int kDirtyGroups = 8;

  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override { return ui::kChangedSpectrum | ui::kChangedHost; }
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  static void onDraw(lv_event_t* e);

  lv_obj_t* area_ = nullptr;
  lv_color_t color_ = {};
  uint16_t heights_[kMaxBars] = {};
  int count_ = 0, x0_ = 0, slot_ = 0, barW_ = 0, h_ = 0;
};

}  // namespace widgets
}  // namespace rp
