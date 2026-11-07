// Small helpers shared by widget implementations.
#pragma once

#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui/theme.h"

namespace rp {
namespace widgets {

inline lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text = "") {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, text);
  return l;
}

// Single-line label that scrolls (marquee) when the text is wider than `width`.
inline lv_obj_t* makeMarquee(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, int width) {
  lv_obj_t* l = makeLabel(parent, font, color);
  lv_obj_set_width(l, width);
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  // 30 px/s at a 33 ms refresh period: the text moves exactly 1 px per frame, one small redraw each.
  lv_obj_set_style_anim_duration(l, lv_anim_speed(30), 0);
  return l;
}

// Single-line label that ends with "..." when too long (secondary text, no animation cost).
inline lv_obj_t* makeEllipsis(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, int width) {
  lv_obj_t* l = makeLabel(parent, font, color);
  lv_obj_set_width(l, width);
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
  return l;
}

// Sets label text only when it changed (avoids invalidating the area for nothing).
inline void setText(lv_obj_t* label, const char* text) {
  const char* cur = lv_label_get_text(label);
  if (cur && strcmp(cur, text) == 0) return;
  lv_label_set_text(label, text);
}

inline void fmtDuration(char* buf, size_t n, uint32_t ms) {
  const uint32_t s = ms / 1000;
  if (s >= 3600) {
    snprintf(buf, n, "%lu:%02lu:%02lu", static_cast<unsigned long>(s / 3600),
             static_cast<unsigned long>((s / 60) % 60), static_cast<unsigned long>(s % 60));
  } else {
    snprintf(buf, n, "%lu:%02lu", static_cast<unsigned long>(s / 60), static_cast<unsigned long>(s % 60));
  }
}

// Value with automatic decimals: 1 decimal below 10, none above.
inline void fmtValue(char* buf, size_t n, float v, int decimals, const char* unit) {
  if (decimals < 0) decimals = fabsf(v) < 10.0f ? 1 : 0;
  // "62°C", "12.5 %", "830 kB/s": no space before a degree sign.
  const bool space = unit && *unit && strncmp(unit, "\xC2\xB0", 2) != 0;
  snprintf(buf, n, "%.*f%s%s", decimals, v, space ? " " : "", unit ? unit : "");
}

inline lv_color_t levelColor(float v, float warn, float crit) {
  if (!isnan(crit) && v >= crit) return ui::color::crit();
  if (!isnan(warn) && v >= warn) return ui::color::warn();
  return ui::color::accent();
}

// Short text for a WMO weather code (Open-Meteo). Icons come in Phase 8.
inline const char* wmoText(uint8_t code) {
  switch (code) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Cloudy";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: case 82: return "Showers";
    case 85: case 86: return "Snow showers";
    case 95: case 96: case 99: return "Thunderstorm";
    default: return "?";
  }
}

}  // namespace widgets
}  // namespace rp
