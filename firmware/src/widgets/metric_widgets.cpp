// System metric widgets: metric_value, metric_bar, metric_gauge, metric_graph.
#include "metric_widgets.h"

#include "common.h"

namespace rp {
namespace widgets {

bool MetricWidget::value(float& out) const {
  const ui::Stat* s = spec_.source ? ui::findStat(spec_.source) : nullptr;
  if (!s) return false;
  out = s->value;
  return true;
}

void MetricWidget::format(char* buf, size_t n, float v) const {
  // Network rates: switch to MB/s from 1000 kB/s so the number stays short.
  if (spec_.unit && strcmp(spec_.unit, "kB/s") == 0 && v >= 1000.0f) {
    fmtValue(buf, n, v / 1000.0f, spec_.decimals < 0 ? 1 : spec_.decimals, "MB/s");
    return;
  }
  fmtValue(buf, n, v, spec_.decimals, spec_.unit);
}

namespace {

lv_obj_t* titleLabel(lv_obj_t* card, const char* text) {
  lv_obj_t* l = makeLabel(card, ui::font::small(), ui::color::dim(), text);
  lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
  return l;
}

}  // namespace

// ---- metric_value ----

void MetricValue::build(lv_obj_t* card, int w, int h) {
  (void)w;
  titleLabel(card, labelText());
  value_ = makeLabel(card, h >= 80 ? ui::font::large() : ui::font::body(), ui::color::text(), "--");
  lv_obj_align(value_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

void MetricValue::refresh(const ui::Model&, uint32_t, uint32_t) {
  float v = 0;
  char buf[24] = "--";
  if (value(v)) format(buf, sizeof(buf), v);
  setText(value_, buf);
}

// ---- metric_bar ----

void MetricBar::build(lv_obj_t* card, int w, int h) {
  (void)h;
  titleLabel(card, labelText());
  value_ = makeLabel(card, ui::font::body(), ui::color::text(), "--");
  lv_obj_align(value_, LV_ALIGN_TOP_RIGHT, 0, -2);
  bar_ = lv_bar_create(card);
  lv_obj_set_size(bar_, w, 14);
  lv_obj_align(bar_, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_bar_set_range(bar_, static_cast<int32_t>(spec_.min * 10), static_cast<int32_t>(spec_.max * 10));
  lv_obj_set_style_bg_color(bar_, ui::color::faint(), LV_PART_MAIN);
  lv_obj_set_style_anim_duration(bar_, 300, 0);
}

void MetricBar::refresh(const ui::Model&, uint32_t, uint32_t) {
  float v = 0;
  char buf[24] = "--";
  const bool ok = value(v);
  if (ok) format(buf, sizeof(buf), v);
  setText(value_, buf);
  lv_bar_set_value(bar_, ok ? static_cast<int32_t>(v * 10) : static_cast<int32_t>(spec_.min * 10), LV_ANIM_ON);
  lv_obj_set_style_bg_color(bar_, levelColor(v, spec_.warn, spec_.crit), LV_PART_INDICATOR);
}

// ---- metric_gauge ----

void MetricGauge::build(lv_obj_t* card, int w, int h) {
  const bool big = h >= 150;
  const int labelH = big ? 0 : 16;
  int side = (w < h - labelH ? w : h - labelH);
  arc_ = lv_arc_create(card);
  lv_obj_set_size(arc_, side, side);
  lv_obj_align(arc_, LV_ALIGN_TOP_MID, 0, 0);
  lv_arc_set_rotation(arc_, 135);
  lv_arc_set_bg_angles(arc_, 0, 270);
  lv_arc_set_range(arc_, static_cast<int32_t>(spec_.min * 10), static_cast<int32_t>(spec_.max * 10));
  lv_obj_remove_style(arc_, nullptr, LV_PART_KNOB);
  lv_obj_set_clickable(arc_, false);
  const int thick = big ? 12 : 7;
  lv_obj_set_style_arc_width(arc_, thick, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc_, thick, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc_, ui::color::faint(), LV_PART_MAIN);
  value_ = makeLabel(card, big ? ui::font::large() : ui::font::body(), ui::color::text(), "--");
  lv_obj_align_to(value_, arc_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_t* l = makeLabel(card, ui::font::small(), ui::color::dim(), labelText());
  if (big) lv_obj_align_to(l, arc_, LV_ALIGN_CENTER, 0, 30);
  else lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, 2);
}

void MetricGauge::refresh(const ui::Model&, uint32_t, uint32_t) {
  float v = 0;
  char buf[24] = "--";
  const bool ok = value(v);
  if (ok) format(buf, sizeof(buf), v);
  setText(value_, buf);
  lv_arc_set_value(arc_, ok ? static_cast<int32_t>(v * 10) : static_cast<int32_t>(spec_.min * 10));
  lv_obj_set_style_arc_color(arc_, levelColor(v, spec_.warn, spec_.crit), LV_PART_INDICATOR);
}

// ---- metric_graph ----

void MetricGraph::build(lv_obj_t* card, int w, int h) {
  titleLabel(card, labelText());
  value_ = makeLabel(card, ui::font::small(), ui::color::text(), "--");
  lv_obj_align(value_, LV_ALIGN_TOP_RIGHT, 0, 0);
  chart_ = lv_chart_create(card);
  lv_obj_set_size(chart_, w, h - 20);
  lv_obj_align(chart_, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_chart_set_type(chart_, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chart_, w >= 300 ? 120 : 60);
  lv_chart_set_update_mode(chart_, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_div_line_count(chart_, 0, 0);
  lv_chart_set_axis_range(chart_, LV_CHART_AXIS_PRIMARY_Y, static_cast<int32_t>(spec_.min * 10),
                          static_cast<int32_t>(spec_.max * 10));
  lv_obj_set_style_bg_opa(chart_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chart_, 0, 0);
  lv_obj_set_style_pad_all(chart_, 0, 0);
  lv_obj_set_style_line_width(chart_, 2, LV_PART_ITEMS);
  lv_obj_set_style_size(chart_, 0, 0, LV_PART_INDICATOR);  // no point markers
  series_ = lv_chart_add_series(chart_, ui::color::accent(), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_all_values(chart_, series_, LV_CHART_POINT_NONE);
}

void MetricGraph::refresh(const ui::Model&, uint32_t changed, uint32_t) {
  float v = 0;
  char buf[24] = "--";
  const bool ok = value(v);
  if (ok) format(buf, sizeof(buf), v);
  setText(value_, buf);
  if (changed & ui::kChangedStats) {
    lv_chart_set_next_value(chart_, series_, ok ? static_cast<int32_t>(v * 10) : LV_CHART_POINT_NONE);
  }
}

}  // namespace widgets
}  // namespace rp
