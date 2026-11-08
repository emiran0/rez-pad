// System metric widgets: metric_value, metric_bar, metric_gauge, metric_graph. Each accepts any
// card size above its minimum (lib/config_logic) and picks fonts to fit; web/preview.js mirrors it.
#include "metric_widgets.h"

#include "common.h"

namespace rp {
namespace widgets {

bool MetricWidget::value(float& out) const {
  const ui::Stat* s = spec_.source[0] ? ui::findStat(spec_.source) : nullptr;
  if (!s) return false;
  out = s->value;
  return true;
}

void MetricWidget::format(char* buf, size_t n, float v) const {
  // Network rates: switch to MB/s from 1000 kB/s so the number stays short.
  if (strcmp(spec_.unit, "kB/s") == 0 && v >= 1000.0f) {
    fmtValue(buf, n, v / 1000.0f, spec_.decimals < 0 ? 1 : spec_.decimals, "MB/s");
    return;
  }
  fmtValue(buf, n, v, spec_.decimals, spec_.unit);
}

const char* MetricWidget::number(char* buf, size_t n, float v) const {
  const char* unit = spec_.unit;
  int decimals = spec_.decimals;
  if (strcmp(unit, "kB/s") == 0 && v >= 1000.0f) {
    v /= 1000.0f;
    unit = "MB/s";
    if (decimals < 0) decimals = 1;
  }
  fmtValue(buf, n, v, decimals, nullptr);
  return unit;
}

namespace {

lv_obj_t* titleLabel(lv_obj_t* card, const char* text, int w) {
  lv_obj_t* l = makeEllipsis(card, ui::font::small(), ui::color::dim(), w);
  lv_label_set_text(l, text);
  lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
  return l;
}

// Largest font whose line fits `h` and whose `chars` digits fit `w`.
const lv_font_t* fitFont(int w, int h, int chars) {
  const lv_font_t* fonts[] = {ui::font::huge(), ui::font::large(), ui::font::body(), ui::font::small()};
  for (const lv_font_t* f : fonts) {
    if (lv_font_get_line_height(f) <= h && static_cast<int>(lv_font_get_glyph_width(f, '8', '8')) * chars <= w) {
      return f;
    }
  }
  return ui::font::small();
}

}  // namespace

// ---- metric_value ----

void MetricValue::build(lv_obj_t* card, int w, int h) {
  const bool title = h >= 40;
  if (title) titleLabel(card, labelText(), w);
  // Room for about 7 characters ("12.5 kB"). The huge font only has digits and the degree sign,
  // so units would fall back to boxes: stop at the large font.
  const lv_font_t* f = fitFont(w, h - (title ? 18 : 0), 7);
  if (f == ui::font::huge()) f = ui::font::large();
  value_ = makeLabel(card, f, ui::color::text(), "--");
  lv_obj_align(value_, title ? LV_ALIGN_BOTTOM_LEFT : LV_ALIGN_LEFT_MID, 0, 0);
}

void MetricValue::refresh(const ui::Model&, uint32_t, uint32_t) {
  float v = 0;
  char buf[24] = "--";
  if (value(v)) format(buf, sizeof(buf), v);
  setText(value_, buf);
}

// ---- metric_bar ----

void MetricBar::build(lv_obj_t* card, int w, int h) {
  bar_ = lv_bar_create(card);
  if (h >= 44) {
    // Label and value on top, bar below.
    titleLabel(card, labelText(), w - 80);
    value_ = makeLabel(card, h >= 70 ? ui::font::large() : ui::font::body(), ui::color::text(), "--");
    lv_obj_align(value_, LV_ALIGN_TOP_RIGHT, 0, -2);
    lv_obj_set_size(bar_, w, h >= 70 ? 18 : 12);
    lv_obj_align(bar_, LV_ALIGN_BOTTOM_MID, 0, -2);
  } else {
    // One row: bar, then the value.
    value_ = makeLabel(card, ui::font::small(), ui::color::text(), "--");
    lv_obj_align(value_, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_size(bar_, w - 64, 10);
    lv_obj_align(bar_, LV_ALIGN_LEFT_MID, 0, 0);
  }
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
  // Label under the ring when the card is not much taller than wide; otherwise (and for big
  // gauges) in the gap at the bottom of the ring.
  int side = w < h ? w : h;
  bool labelBelow = false;
  if (side >= h - 4 && side < 130) {
    side = w < h - 16 ? w : h - 16;
    labelBelow = true;
  }
  const int thick = side >= 130 ? 12 : side >= 80 ? 8 : 6;
  arc_ = lv_arc_create(card);
  lv_obj_set_size(arc_, side, side);
  lv_obj_align(arc_, LV_ALIGN_TOP_MID, 0, 0);
  lv_arc_set_rotation(arc_, 135);
  lv_arc_set_bg_angles(arc_, 0, 270);
  lv_arc_set_range(arc_, static_cast<int32_t>(spec_.min * 10), static_cast<int32_t>(spec_.max * 10));
  lv_obj_remove_style(arc_, nullptr, LV_PART_KNOB);
  lv_obj_set_clickable(arc_, false);
  lv_obj_set_style_arc_width(arc_, thick, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc_, thick, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc_, ui::color::faint(), LV_PART_MAIN);

  // Inside the ring: the number (up to 4 digits) and, if it fits, the unit in small type below.
  const int inner = side - 2 * thick - 6;
  const bool unit = spec_.unit[0] && inner >= 44;
  const lv_font_t* f = fitFont(inner, inner - (unit ? 16 : 0), 4);
  value_ = makeLabel(card, f, ui::color::text(), "--");
  const int numH = lv_font_get_line_height(f);
  const int top = side / 2 - (unit ? (numH + 14) / 2 : numH / 2);
  lv_obj_align(value_, LV_ALIGN_TOP_MID, 0, top);
  if (unit) {
    unit_ = makeLabel(card, ui::font::small(), ui::color::dim(), spec_.unit);
    lv_obj_align(unit_, LV_ALIGN_TOP_MID, 0, top + numH - 2);
  }
  lv_obj_t* l = makeEllipsis(card, ui::font::small(), ui::color::dim(), labelBelow ? w : inner + 16);
  lv_label_set_text(l, labelText());
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  if (labelBelow) lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, 2);
  else lv_obj_align(l, LV_ALIGN_TOP_MID, 0, side - 18);
}

void MetricGauge::refresh(const ui::Model&, uint32_t, uint32_t) {
  float v = 0;
  char buf[24] = "--";
  const bool ok = value(v);
  const char* unit = spec_.unit;
  if (ok) unit = number(buf, sizeof(buf), v);
  setText(value_, buf);
  if (unit_) setText(unit_, unit);
  lv_arc_set_value(arc_, ok ? static_cast<int32_t>(v * 10) : static_cast<int32_t>(spec_.min * 10));
  lv_obj_set_style_arc_color(arc_, levelColor(v, spec_.warn, spec_.crit), LV_PART_INDICATOR);
}

// ---- metric_graph ----

void MetricGraph::build(lv_obj_t* card, int w, int h) {
  const bool header = h >= 56;
  if (header) titleLabel(card, labelText(), w - 70);
  const int chartH = header ? h - 20 : h;
  chart_ = lv_chart_create(card);
  lv_obj_set_size(chart_, w, chartH);
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
  // Value top right, over the line when there is no header.
  value_ = makeLabel(card, ui::font::small(), ui::color::text(), "--");
  lv_obj_align(value_, LV_ALIGN_TOP_RIGHT, 0, 0);
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
