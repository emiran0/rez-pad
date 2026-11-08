#pragma once

#include "widget.h"

namespace rp {
namespace widgets {

// Base for widgets showing one numeric data source (spec().source).
class MetricWidget : public Widget {
 public:
  using Widget::Widget;
  uint32_t interests() const override { return ui::kChangedStats | ui::kChangedHost; }

 protected:
  // Current value, or false when the source is unknown or not available.
  bool value(float& out) const;
  void format(char* buf, size_t n, float v) const;
  // The number alone; returns the unit to show with it (kB/s becomes MB/s from 1000).
  const char* number(char* buf, size_t n, float v) const;
  const char* labelText() const { return spec_.label[0] ? spec_.label : spec_.source; }
};

class MetricValue : public MetricWidget {
 public:
  using MetricWidget::MetricWidget;
  void build(lv_obj_t* card, int w, int h) override;
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  lv_obj_t* value_ = nullptr;
};

class MetricBar : public MetricWidget {
 public:
  using MetricWidget::MetricWidget;
  void build(lv_obj_t* card, int w, int h) override;
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  lv_obj_t* value_ = nullptr;
  lv_obj_t* bar_ = nullptr;
};

class MetricGauge : public MetricWidget {
 public:
  using MetricWidget::MetricWidget;
  void build(lv_obj_t* card, int w, int h) override;
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  lv_obj_t* arc_ = nullptr;
  lv_obj_t* value_ = nullptr;  // the number only, sized to fit inside the ring
  lv_obj_t* unit_ = nullptr;   // smaller, under the number (nullptr when there is no room)
};

// Sparkline. Keeps its own history ring (one point per stats update, also while hidden).
class MetricGraph : public MetricWidget {
 public:
  using MetricWidget::MetricWidget;
  void build(lv_obj_t* card, int w, int h) override;
  void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) override;

 private:
  lv_obj_t* value_ = nullptr;
  lv_obj_t* chart_ = nullptr;
  lv_chart_series_t* series_ = nullptr;
};

}  // namespace widgets
}  // namespace rp
