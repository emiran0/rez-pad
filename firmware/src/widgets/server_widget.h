#pragma once

#include "widget.h"

namespace rp {
namespace widgets {

// server: one Beszel machine (sources bz.<system>.*, cfg::kServerMetrics): name and online dot,
// CPU / memory / disk bars as far as the height allows, and a footer with load, temperature and
// network down/up when there is room.
class ServerWidget : public Widget {
 public:
  using Widget::Widget;
  void build(lv_obj_t* card, int w, int h) override;
  uint32_t interests() const override { return ui::kChangedStats | ui::kChangedHost; }
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  struct Row {
    lv_obj_t* bar = nullptr;
    lv_obj_t* value = nullptr;
  };
  static constexpr int kRows = 3;  // CPU, memory, disk

  const ui::Stat* stat(const char* metric) const;

  lv_obj_t* dot_ = nullptr;
  lv_obj_t* footer_ = nullptr;
  Row rows_[kRows];
  int rowCount_ = 0;
};

}  // namespace widgets
}  // namespace rp
