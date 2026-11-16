// Weather and room widgets: weather_now, weather_forecast, room_climate, clock.
#include "weather_widgets.h"

#include <time.h>

#include "common.h"
#include "weather/weather.h"
#include "weather_icons.h"

namespace rp {
namespace widgets {

namespace {

const char* const kWeekdays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Day of week (0 = Sunday) for a Gregorian date (Sakamoto's method).
int weekday(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[(m - 1) % 12] + d) % 7;
}

// Local calendar time from the model's synced clock; false when the clock is unknown.
bool localTm(const ui::Model& m, int64_t unix, struct tm& out) {
  if (unix <= 0) return false;
  const time_t local = static_cast<time_t>(unix + static_cast<int64_t>(m.tzOffsetMin) * 60);
  gmtime_r(&local, &out);
  return true;
}

rp_wx_icon_t wmoIcon(uint8_t code, bool day) {
  switch (code) {
    case 0: case 1: return day ? RP_WX_CLEAR_DAY : RP_WX_CLEAR_NIGHT;
    case 2: return day ? RP_WX_PARTLY_DAY : RP_WX_PARTLY_NIGHT;
    case 3: return RP_WX_CLOUDY;
    case 45: case 48: return RP_WX_FOG;
    case 51: case 53: case 55: return RP_WX_DRIZZLE;
    case 56: case 57: case 66: case 67: return RP_WX_SLEET;
    case 61: case 63: case 80: case 81: return RP_WX_RAIN;
    case 65: case 82: return RP_WX_HEAVY_RAIN;
    case 71: case 73: case 75: case 77: case 85: case 86: return RP_WX_SNOW;
    case 95: case 96: case 99: return RP_WX_THUNDER;
    default: return RP_WX_CLOUDY;
  }
}

void setIcon(lv_obj_t* img, uint8_t code, bool day, int px) {
  const lv_image_dsc_t* dsc = rp_weather_icon(wmoIcon(code, day), px);
  if (lv_image_get_src(img) != dsc) lv_image_set_src(img, dsc);
}

const char* windUnit(const ui::Units& u) { return u.wind == 1 ? "m/s" : (u.wind == 2 ? "mph" : "km/h"); }

// Weather is stale (hidden) without data, or 12 h after the host fetched it.
bool fresh(const ui::Model& m) {
  const auto& w = m.weather;
  return w.valid && (m.unixTime == 0 || m.unixTime - w.updated < weather::kMaxAgeS);
}

void ageText(char* buf, size_t n, const ui::Model& m) {
  const int64_t age = m.unixTime > 0 ? m.unixTime - m.weather.updated : -1;
  if (age < 0) buf[0] = '\0';
  else if (age < 120) snprintf(buf, n, "now");
  else if (age < 3600) snprintf(buf, n, "%d min ago", static_cast<int>(age / 60));
  else snprintf(buf, n, "%d h ago", static_cast<int>(age / 3600));
}

}  // namespace

// ---- weather_now ----

void WeatherNow::build(lv_obj_t* card, int w, int h) {
  tall_ = h >= 140 && w >= 160;
  const bool narrow = !tall_ && w < 140;
  iconPx_ = tall_ ? 72 : (h >= 70 && !narrow) || (narrow && h >= 80 && w >= 110) ? 48 : 32;
  icon_ = lv_image_create(card);
  lv_obj_set_hidden(icon_, true);
  if (narrow) {
    // Small card: icon and temperature on top, condition below; no location or age.
    lv_obj_align(icon_, LV_ALIGN_TOP_LEFT, 0, 0);
    temp_ = makeLabel(card, h >= 50 ? ui::font::large() : ui::font::body(), ui::color::text(), "--");
    lv_obj_align(temp_, LV_ALIGN_TOP_RIGHT, 0, 0);
    cond_ = makeEllipsis(card, ui::font::small(), ui::color::dim(), w);
    lv_obj_align(cond_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  } else if (tall_) {
    loc_ = makeEllipsis(card, ui::font::small(), ui::color::accent(), w - 80);
    lv_obj_align(loc_, LV_ALIGN_TOP_LEFT, 0, 0);
    age_ = makeLabel(card, ui::font::small(), ui::color::dim(), "");
    lv_obj_align(age_, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_align(icon_, LV_ALIGN_TOP_RIGHT, 0, 18);
    temp_ = makeLabel(card, ui::font::huge(), ui::color::text(), "--");
    lv_obj_align(temp_, LV_ALIGN_TOP_LEFT, 0, 24);
    cond_ = makeEllipsis(card, ui::font::body(), ui::color::text(), w);
    lv_obj_align(cond_, LV_ALIGN_TOP_LEFT, 0, 94);
    details_ = makeLabel(card, ui::font::small(), ui::color::dim(), "");
    lv_obj_align(details_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  } else {
    const int x = iconPx_ + 8;
    lv_obj_align(icon_, LV_ALIGN_LEFT_MID, 0, 0);
    // The location and the age ("2 h ago") share the top line; on a small card the age goes and
    // the location gets the line (it would be cut to a few letters).
    const bool withAge = w - x - 70 >= 60;
    loc_ = makeEllipsis(card, ui::font::small(), ui::color::accent(), withAge ? w - x - 70 : w - x);
    lv_obj_align(loc_, LV_ALIGN_TOP_LEFT, x, 0);
    if (withAge) {
      age_ = makeLabel(card, ui::font::small(), ui::color::dim(), "");
      lv_obj_align(age_, LV_ALIGN_TOP_RIGHT, 0, 0);
    }
    temp_ = makeLabel(card, ui::font::large(), ui::color::text(), "--");
    lv_obj_align(temp_, LV_ALIGN_LEFT_MID, x, 2);
    cond_ = makeEllipsis(card, ui::font::small(), ui::color::dim(), w - x);
    lv_obj_align(cond_, LV_ALIGN_BOTTOM_LEFT, x, 0);
  }
}

void WeatherNow::refresh(const ui::Model& m, uint32_t, uint32_t) {
  const auto& wx = m.weather;
  if (!fresh(m)) {
    if (loc_) setText(loc_, wx.valid ? wx.loc : "Weather");
    if (age_) setText(age_, "");
    setText(temp_, "--");
    setText(cond_, m.hostConnected ? "waiting for data" : "no data");
    if (details_) setText(details_, "");
    lv_obj_set_hidden(icon_, true);
    return;
  }
  char buf[80];
  if (loc_) setText(loc_, wx.loc);
  ageText(buf, sizeof(buf), m);
  if (age_) setText(age_, buf);
  snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", wx.temp);
  setText(temp_, buf);
  setText(cond_, wmoText(wx.code));
  setIcon(icon_, wx.code, wx.isDay, iconPx_);
  lv_obj_set_hidden(icon_, false);
  if (details_) {
    char press[24] = "";
    if (wx.hasPress) snprintf(press, sizeof(press), m.units.inHg ? "   %.2f inHg" : "   %.0f hPa", wx.press);
    snprintf(buf, sizeof(buf), "Feels %.0f\xC2\xB0   Hum %u %%\nWind %.*f %s%s", wx.feels, wx.hum,
             m.units.wind == 1 ? 1 : 0, wx.wind, windUnit(m.units), press);
    setText(details_, buf);
  }
}

// ---- weather_forecast ----

void WeatherForecast::build(lv_obj_t* card, int w, int h) {
  hourly_ = strcmp(spec_.mode, "hourly") == 0;
  // As many columns as fit (about 48 px each), up to 5; icons when there is height for them.
  count_ = w / 48;
  if (count_ > kCols) count_ = kCols;
  if (count_ < 1) count_ = 1;
  const int colW = w / count_;
  const bool icons = h >= 60;
  const lv_font_t* tempF = colW >= 70 && h >= 60 ? ui::font::body() : ui::font::small();
  for (int i = 0; i < count_; ++i) {
    lv_obj_t* col = lv_obj_create(card);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, colW, LV_PCT(100));
    lv_obj_set_pos(col, i * colW, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label_[i] = makeLabel(col, ui::font::small(), ui::color::dim(), "--");
    if (icons) {
      icon_[i] = lv_image_create(col);
      lv_obj_set_size(icon_[i], 32, 32);
    }
    temps_[i] = makeLabel(col, tempF, ui::color::text(), "");
    lv_label_set_recolor(temps_[i], true);
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
  const bool ok = fresh(m);
  struct tm today = {};
  const bool haveToday = localTm(m, m.unixTime, today);
  char buf[40];
  char dim[8];
  snprintf(dim, sizeof(dim), "%06lX", static_cast<unsigned long>(ui::palette().dim));
  for (int i = 0; i < count_; ++i) {
    bool show = false;
    if (ok && hourly_) {
      // Every 2 h from the next hour (hourly[0] is the current hour).
      const int idx = 1 + i * 2;
      if (idx < wx.hourCount) {
        const auto& hr = wx.hourly[idx];
        struct tm t = {};
        localTm(m, hr.t, t);
        snprintf(buf, sizeof(buf), "%02d:00", t.tm_hour);
        setText(label_[i], buf);
        if (icon_[i]) setIcon(icon_[i], hr.code, hr.isDay, 32);
        snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", hr.temp);
        setText(temps_[i], buf);
        show = true;
      }
    } else if (ok && i < wx.dayCount) {
      const auto& d = wx.daily[i];
      const bool isToday = haveToday && d.year == today.tm_year + 1900 && d.month == today.tm_mon + 1 &&
                           d.day == today.tm_mday;
      setText(label_[i], isToday ? "Today" : kWeekdays[weekday(d.year, d.month, d.day)]);
      if (icon_[i]) setIcon(icon_[i], d.code, true, 32);
      snprintf(buf, sizeof(buf), "%.0f\xC2\xB0 #%s %.0f\xC2\xB0#", d.max, dim, d.min);
      setText(temps_[i], buf);
      show = true;
    }
    if (!show) {
      setText(label_[i], "--");
      setText(temps_[i], "");
    }
    if (icon_[i]) lv_obj_set_hidden(icon_[i], !show);
  }
}

// ---- room_climate ----

void RoomClimate::build(lv_obj_t* card, int w, int h) {
  tall_ = h >= 140 && w >= 140;
  if (!tall_ && w < 160) {
    // Small card: temperature, humidity under it; pressure only when there is height.
    if (h >= 60) makeLabel(card, ui::font::small(), ui::color::accent(), "Room");
    temp_ = makeLabel(card, h >= 50 ? ui::font::large() : ui::font::body(), ui::color::text(), "--");
    lv_obj_align(temp_, h >= 60 ? LV_ALIGN_LEFT_MID : LV_ALIGN_TOP_LEFT, 0, h >= 60 ? 0 : -2);
    hum_ = makeLabel(card, ui::font::small(), ui::color::text(), "");
    lv_obj_align(hum_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    press_ = makeLabel(card, ui::font::small(), ui::color::dim(), "");
    lv_obj_align(press_, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_hidden(press_, h < 70 || w < 120);
    trend_ = makeLabel(card, ui::font::small(), ui::color::dim(), "");
    lv_obj_set_hidden(trend_, true);
    return;
  }
  makeLabel(card, ui::font::small(), ui::color::accent(), "Room");
  temp_ = makeLabel(card, tall_ ? ui::font::huge() : ui::font::large(), ui::color::text(), "--");
  lv_obj_align(temp_, tall_ ? LV_ALIGN_TOP_LEFT : LV_ALIGN_LEFT_MID, 0, tall_ ? 24 : 6);
  hum_ = makeLabel(card, ui::font::body(), ui::color::text(), "");
  press_ = makeLabel(card, ui::font::small(), ui::color::dim(), "");
  trend_ = makeLabel(card, ui::font::small(), ui::color::dim(), "");
  if (tall_) {
    lv_obj_align(hum_, LV_ALIGN_TOP_LEFT, 0, 94);
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
  const auto& u = m.units;
  char buf[48];
  if (r.tempValid) {
    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", u.fahrenheit ? r.temp * 9.0f / 5.0f + 32.0f : r.temp);
    setText(temp_, buf);
    snprintf(buf, sizeof(buf), "%.0f %% RH", r.hum);
    setText(hum_, buf);
  } else {
    setText(temp_, "--");
    setText(hum_, r.samples ? "sensor error" : "");
  }
  if (r.pressValid) {
    if (u.inHg) snprintf(buf, sizeof(buf), "%.2f inHg", r.press * 0.0295300f);
    else snprintf(buf, sizeof(buf), "%.0f hPa", r.press);
    setText(press_, buf);
  } else {
    setText(press_, "");
  }
  const float d = u.inHg ? r.delta3h * 0.0295300f : r.delta3h;
  const char* unit = u.inHg ? "inHg" : "hPa";
  switch (r.trend) {
    case 1: snprintf(buf, sizeof(buf), LV_SYMBOL_DOWN " falling %+.*f %s/3h", u.inHg ? 2 : 1, d, unit); break;
    case 2: snprintf(buf, sizeof(buf), LV_SYMBOL_RIGHT " steady"); break;
    case 3: snprintf(buf, sizeof(buf), LV_SYMBOL_UP " rising %+.*f %s/3h", u.inHg ? 2 : 1, d, unit); break;
    default: snprintf(buf, sizeof(buf), "trend: collecting"); break;
  }
  setText(trend_, buf);
}

// ---- clock ----

void Clock::build(lv_obj_t* card, int w, int h) {
  wide_ = w >= 160;
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 2, 0);
  // "00:00" is 5 characters; the date needs one small line under it.
  const lv_font_t* f = w >= 150 && h >= 70 ? ui::font::huge() : w >= 80 && h >= 40 ? ui::font::large() : ui::font::body();
  time_ = makeLabel(card, f, ui::color::text(), "--:--");
  if (h - lv_font_get_line_height(f) >= 16) date_ = makeLabel(card, ui::font::small(), ui::color::dim(), "");
}

void Clock::refresh(const ui::Model& m, uint32_t, uint32_t) {
  struct tm t = {};
  if (m.clockMinutes < 0 || !localTm(m, m.unixTime, t)) {
    setText(time_, "--:--");
    if (date_) setText(date_, "");
    return;
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "%02d:%02d", m.clockMinutes / 60, m.clockMinutes % 60);
  setText(time_, buf);
  if (wide_) snprintf(buf, sizeof(buf), "%s %d %s", kWeekdays[t.tm_wday], t.tm_mday, kMonths[t.tm_mon]);
  else snprintf(buf, sizeof(buf), "%s %d", kWeekdays[t.tm_wday], t.tm_mday);
  if (date_) setText(date_, buf);
}

}  // namespace widgets
}  // namespace rp
