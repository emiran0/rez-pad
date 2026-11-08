#pragma once

#include "widget.h"

namespace rp {
namespace widgets {

// The Windows volume mixer (MIXER). Layout "list": one row per app (name, level, horizontal bar);
// "faders": one column per app (level, vertical bar, name). Focused with mixer.focus (accent ring,
// the selected app marked); see src/audio/mixer.h.
class MixerWidget : public Widget {
 public:
  static constexpr int kMaxSlots = ui::kMixerApps;

  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override { return ui::kChangedMixer | ui::kChangedHost; }
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  struct Slot {
    lv_obj_t* bg = nullptr;      // selection highlight
    lv_obj_t* marker = nullptr;  // accent edge of the selection
    lv_obj_t* name = nullptr;
    lv_obj_t* level = nullptr;
    lv_obj_t* track = nullptr;
    lv_obj_t* fill = nullptr;
  };

  lv_obj_t* card_ = nullptr;
  lv_obj_t* message_ = nullptr;
  Slot slots_[kMaxSlots];
  int slotCount_ = 0;
  bool faders_ = false;
  int trackLen_ = 0;  // bar length in px (width for rows, height for faders)
  int first_ = 0;     // first app shown (scrolls to keep the selection visible)
};

}  // namespace widgets
}  // namespace rp
