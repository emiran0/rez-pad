// Weather and room widgets: weather_now, weather_forecast, room_climate.
// Weather icons for WMO codes come in Phase 8; until then the condition is shown as text.
#include "weather_widgets.h"

#include "common.h"

namespace rp {
namespace widgets {

// ---- weather_now ----

void WeatherNow::build(lv_obj_t* card, int w, int h) {
  tall_ = h >= 150;
  loc_ = makeEllipsis(card, ui::font::small(), ui::color::accent(), w);
  lv_obj_align(loc_, LV_ALIGN_TOP_LEFT, 0, 0);
  temp_ = makeLabel(card, tall_ ? ui::font::huge() : ui::font::large(), ui::color::text(), "--");
  lv_obj_align(temp_, tall_ ? LV_ALIGN_TOP_LEFT : LV_ALIGN_LEFT_MID, 0, tall_ ? 22 : 6);
  cond_ = makeLabel(card, tall_ ? ui::font::body() : ui::font::small(), ui::color::text(), "");
  if (tall_) {
    lv_obj_align(cond_, LV_ALIGN_TOP_LEFT, 0, 82);
  } else {
    lv_obj_align(cond_, LV_ALIGN_RIGHT_MID, 0, 6);
  }
  details_ = makeLabel(card, ui::font::small(), ui::color::dim(), "");
  lv_obj_align(details_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  if (!tall_) lv_obj_set_hidden(details_, true);
}

void WeatherNow::refresh(const ui::Model& m, uint32_t, uint32_t) {
  const auto& wx = m.weather;
  if (!wx.valid) {
    setText(loc_, "Weather");
    setText(temp_, "--");
    setText(cond_, "no data");
    setText(details_, "");
    return;
  }
  char buf[64];
  setText(loc_, wx.loc);
  snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", wx.temp);
  setText(temp_, buf);
  setText(cond_, wmoText(wx.code));
  snprintf(buf, sizeof(buf), "Feels %.0f\xC2\xB0   Hum %u %%\nWind %.0f km/h", wx.feels, wx.hum, wx.wind);
  setText(details_, buf);
}

// ---- weather_forecast ----

void WeatherForecast::build(lv_obj_t* card, int w, int h) {
  (void)h;
  const int colW = w / kCols;
  for (int i = 0; i < kCols; ++i) {
    lv_obj_t* col = lv_obj_create(card);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, colW, LV_PCT(100));
    lv_obj_set_pos(col, i * colW, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    day_[i] = makeLabel(col, ui::font::small(), ui::color::dim(), "--");
    cond_[i] = makeLabel(col, ui::font::small(), ui::color::text(), "");
    lv_obj_set_width(cond_[i], colW - 4);
    lv_label_set_long_mode(cond_[i], LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(cond_[i], LV_TEXT_ALIGN_CENTER, 0);
    temps_[i] = makeLabel(col, ui::font::body(), ui::color::text(), "");
    if (i > 0) {
      lv_obj_t* sep = lv_obj_create(card);
      lv_obj_remove_style_all(sep);
      lv_obj_set_size(sep, 1, LV_PCT(80));
      lv_obj_set_style_bg_color(sep, ui::color::cardBorder(), 0);
      lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
      lv_obj_set_pos(sep, i * colW, 8);
    }
  }
}

void WeatherForecast::refresh(const ui::Model& m, uint32_t, uint32_t) {
  const auto& wx = m.weather;
  for (int i = 0; i < kCols; ++i) {
    if (!wx.valid) {
      setText(day_[i], "--");
      setText(cond_[i], "");
      setText(temps_[i], "");
      continue;
    }
    const auto& d = wx.daily[i];
    char buf[32];
    setText(day_[i], d.name);
    setText(cond_[i], wmoText(d.code));
    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0 #8A939E %.0f\xC2\xB0#", d.max, d.min);
    lv_label_set_recolor(temps_[i], true);
    setText(temps_[i], buf);
  }
}

// ---- room_climate ----

void RoomClimate::build(lv_obj_t* card, int w, int h) {
  (void)w;
  tall_ = h >= 150;
  makeLabel(card, ui::font::small(), ui::color::accent(), "Room");
  temp_ = makeLabel(card, tall_ ? ui::font::huge() : ui::font::large(), ui::color::text(), "--");
  lv_obj_align(temp_, tall_ ? LV_ALIGN_TOP_LEFT : LV_ALIGN_LEFT_MID, 0, tall_ ? 22 : 6);
  hum_ = makeLabel(card, ui::font::body(), ui::color::text(), "");
  press_ = makeLabel(card, ui::font::small(), ui::color::dim(), "");
  trend_ = makeLabel(card, ui::font::small(), ui::color::dim(), "");
  if (tall_) {
    lv_obj_align(hum_, LV_ALIGN_TOP_LEFT, 0, 82);
    lv_obj_align(press_, LV_ALIGN_BOTTOM_LEFT, 0, -20);
    lv_obj_align(trend_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  } else {
    lv_obj_align(hum_, LV_ALIGN_RIGHT_MID, 0, -8);
    lv_obj_align(press_, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_hidden(trend_, true);
  }
}

void RoomClimate::refresh(const ui::Model& m, uint32_t, uint32_t) {
  const auto& r = m.room;
  char buf[48];
  if (r.tempValid) {
    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", r.temp);
    setText(temp_, buf);
    snprintf(buf, sizeof(buf), "%.0f %% RH", r.hum);
    setText(hum_, buf);
  } else {
    setText(temp_, "--");
    setText(hum_, r.samples ? "sensor error" : "");
  }
  if (r.pressValid) {
    snprintf(buf, sizeof(buf), "%.0f hPa", r.press);
    setText(press_, buf);
  } else {
    setText(press_, "");
  }
  switch (r.trend) {
    case 1: snprintf(buf, sizeof(buf), LV_SYMBOL_DOWN " falling %+.1f hPa/3h", r.delta3h); break;
    case 2: snprintf(buf, sizeof(buf), LV_SYMBOL_RIGHT " steady"); break;
    case 3: snprintf(buf, sizeof(buf), LV_SYMBOL_UP " rising %+.1f hPa/3h", r.delta3h); break;
    default: snprintf(buf, sizeof(buf), "trend: collecting"); break;
  }
  setText(trend_, buf);
}

}  // namespace widgets
}  // namespace rp
