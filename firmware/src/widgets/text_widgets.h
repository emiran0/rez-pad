#pragma once

#include "widget.h"

namespace rp {
namespace widgets {

// Static text ("text"), any size; an optional "label" is shown small above it.
class Label : public Widget {
 public:
  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override { return 0; }
  void refresh(const ui::Model&, uint32_t, uint32_t) override {}
};

}  // namespace widgets
}  // namespace rp
