#include "status_bar.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "theme.h"
#include "widgets/common.h"

namespace rp {
namespace ui {
namespace status_bar {

namespace {

constexpr int kMaxDots = 8;
constexpr int kDotSpacing = 12;
constexpr int kItemsRight = 26;   // items end left of the host icon
constexpr int kNameX = 56;        // page name starts right of the clock
constexpr int kNameMinW = 40;     // narrower than this: no page name
constexpr int kWideScreen = 400;  // from this width: 5 dots and up to 3 items

// Layout for the screen width (set by build()): dots shown at once (more pages slide the window),
// status items shown, page name width.
int gWindow = 5;
int gMaxItems = cfg::kMaxStatusItems;
int gNameW = 140;

lv_obj_t* gBar = nullptr;
lv_obj_t* gClock = nullptr;
lv_obj_t* gPageName = nullptr;
lv_obj_t* gDots[kMaxDots] = {};
lv_obj_t* gHost = nullptr;
lv_obj_t* gWarn = nullptr;
lv_obj_t* gHint = nullptr;  // replaces clock, page name and dots while shown (e.g. mixer instructions)
lv_obj_t* gItemRow = nullptr;
lv_obj_t* gItems[cfg::kMaxStatusItems] = {};
cfg::StatusItem gItemCfg[cfg::kMaxStatusItems];
int gCfgCount = 0;   // items in the config
int gItemCount = 0;  // labels shown (fewer on narrow screens, which then cycle through the items)
int gCycle = 0;      // first item shown while cycling
uint32_t gCycleAt = 0;
constexpr uint32_t kCycleMs = 4000;
bool gWide = true;
bool gItemsDirty = true;
bool gHintOn = false;

// Compact unit for the bar: "°C" becomes "°" (the bar has no room for the letter).
const char* compactUnit(const char* unit) {
  return strncmp(unit, "\xC2\xB0", 2) == 0 ? "\xC2\xB0" : unit;
}

void itemText(const cfg::StatusItem& it, const Model& m, char* out, size_t n) {
  const char* label = it.label;
  const char* sep = label[0] ? " " : "";
  switch (it.type) {
    case cfg::StatusType::kDate: {
      if (m.unixTime <= 0) {
        snprintf(out, n, "%s%s--", label, sep);
        return;
      }
      static const char* const kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      const time_t local = static_cast<time_t>(m.unixTime + static_cast<int64_t>(m.tzOffsetMin) * 60);
      struct tm t;
      gmtime_r(&local, &t);
      snprintf(out, n, "%s%s%s %d", label, sep, kDays[t.tm_wday % 7], t.tm_mday);
      return;
    }
    case cfg::StatusType::kRoomTemp: {
      if (!m.room.tempValid) {
        snprintf(out, n, "%s%s--", label, sep);
        return;
      }
      const float v = m.units.fahrenheit ? m.room.temp * 9.0f / 5.0f + 32.0f : m.room.temp;
      snprintf(out, n, "%s%s%.1f\xC2\xB0", label, sep, v);
      return;
    }
    case cfg::StatusType::kRoomHum:
      if (!m.room.tempValid) snprintf(out, n, "%s%s--", label, sep);
      else snprintf(out, n, "%s%s%.0f%%", label, sep, m.room.hum);
      return;
    case cfg::StatusType::kOutdoorTemp:
      // The host sends weather in the display units already.
      if (!m.weather.valid) snprintf(out, n, "%s%s--", label, sep);
      else snprintf(out, n, "%s%s%.0f\xC2\xB0", label, sep, m.weather.temp);
      return;
    case cfg::StatusType::kVolume:
      if (!m.volume.valid) snprintf(out, n, "%s%s--", label[0] ? label : LV_SYMBOL_VOLUME_MAX, " ");
      else if (m.volume.muted) snprintf(out, n, "%s", LV_SYMBOL_MUTE);
      else snprintf(out, n, "%s %u", label[0] ? label : LV_SYMBOL_VOLUME_MAX, m.volume.level);
      return;
    case cfg::StatusType::kMetric: {
      const Stat* s = nullptr;
      for (const Stat& st : m.stats) {
        if (st.valid && strcmp(st.id, it.source) == 0) s = &st;
      }
      const char* unit = compactUnit(cfg::sourceDefaults(it.source).unit);
      if (!s) {
        snprintf(out, n, "%s%s--", label, sep);
        return;
      }
      const bool decimal = fabsf(s->value) < 10.0f && unit[0] != '%';
      if (decimal) snprintf(out, n, "%s%s%.1f%s", label, sep, s->value, unit);
      else snprintf(out, n, "%s%s%.0f%s", label, sep, s->value, unit);
      return;
    }
  }
  out[0] = '\0';
}

}  // namespace

void build(lv_obj_t* parent) {
  const int w = screenW();
  const bool wide = w >= kWideScreen;
  gWide = wide;
  gWindow = wide ? 5 : 3;
  gMaxItems = wide ? cfg::kMaxStatusItems : 1;
  // From the clock to just before the dots (centered on the bar, which has 8 px padding).
  gNameW = w / 2 - 8 - (gWindow - 1) * kDotSpacing / 2 - 10 - kNameX;

  gBar = lv_obj_create(parent);
  lv_obj_remove_style_all(gBar);
  lv_obj_set_size(gBar, w, kStatusH);
  lv_obj_set_pos(gBar, 0, 0);
  lv_obj_set_style_bg_color(gBar, color::status(), 0);
  lv_obj_set_style_bg_opa(gBar, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(gBar, 8, 0);
  lv_obj_set_scrollable(gBar, false);

  gClock = widgets::makeLabel(gBar, font::small(), color::text(), "--:--");
  lv_obj_align(gClock, LV_ALIGN_LEFT_MID, 0, 0);

  gPageName = widgets::makeEllipsis(gBar, font::small(), color::dim(), gNameW > kNameMinW ? gNameW : kNameMinW);
  lv_obj_set_height(gPageName, lv_font_get_line_height(font::small()));  // one line, cut with "..."
  lv_obj_align(gPageName, LV_ALIGN_LEFT_MID, kNameX, 0);
  lv_obj_set_hidden(gPageName, gNameW < kNameMinW);

  for (int i = 0; i < kMaxDots; ++i) {
    gDots[i] = lv_obj_create(gBar);
    lv_obj_remove_style_all(gDots[i]);
    lv_obj_set_size(gDots[i], 6, 6);
    lv_obj_set_style_radius(gDots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(gDots[i], LV_OPA_COVER, 0);
    lv_obj_set_hidden(gDots[i], true);
  }

  // Status items: a row aligned to the right, left of the host icon.
  gItemRow = lv_obj_create(gBar);
  lv_obj_remove_style_all(gItemRow);
  lv_obj_set_scrollable(gItemRow, false);
  lv_obj_set_size(gItemRow, LV_SIZE_CONTENT, kStatusH);
  lv_obj_set_layout(gItemRow, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(gItemRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(gItemRow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(gItemRow, 12, 0);
  lv_obj_align(gItemRow, LV_ALIGN_RIGHT_MID, -kItemsRight, 0);
  // Never over the dots (LVGL clips what does not fit).
  lv_obj_set_style_max_width(gItemRow, w - 16 - kItemsRight - (w / 2 - 8 + (gWindow - 1) * kDotSpacing / 2 + 10), 0);
  for (int i = 0; i < cfg::kMaxStatusItems; ++i) {
    gItems[i] = widgets::makeLabel(gItemRow, font::small(), color::text(), "");
    lv_obj_set_hidden(gItems[i], true);
  }

  gHint = widgets::makeEllipsis(gBar, font::small(), color::accent(), w - 16 - kItemsRight);
  lv_obj_set_height(gHint, lv_font_get_line_height(font::small()));
  lv_obj_align(gHint, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_hidden(gHint, true);

  gHost = widgets::makeLabel(gBar, font::small(), color::faint(), LV_SYMBOL_USB);
  lv_obj_align(gHost, LV_ALIGN_RIGHT_MID, 0, 0);

  gWarn = widgets::makeLabel(gBar, font::small(), color::warn(), LV_SYMBOL_WARNING " config");
  lv_obj_align(gWarn, LV_ALIGN_RIGHT_MID, -22, 0);
  lv_obj_set_hidden(gWarn, true);
}

void setItems(const cfg::Config& c) {
  gCfgCount = c.statusCount;
  gItemCount = c.statusCount < gMaxItems ? c.statusCount : gMaxItems;
  gCycle = 0;
  gCycleAt = millis();
  for (int i = 0; i < cfg::kMaxStatusItems; ++i) {
    if (i < gCfgCount) gItemCfg[i] = c.status[i];
    lv_obj_set_hidden(gItems[i], i >= gItemCount || gHintOn);
  }
  gItemsDirty = true;
}

void setWarning(const char* text) {
  // The bar has no room for the reason; it is in the serial log and the host's status page.
  const bool on = text && text[0];
  lv_obj_set_hidden(gWarn, !on);
  lv_obj_set_hidden(gItemRow, on);  // the warning takes the items' place
}

void setHint(const char* text) {
  // A narrow bar has no room for a hint next to anything; it shows none.
  gHintOn = gWide && text && text[0];
  lv_obj_set_hidden(gHint, !gHintOn);
  lv_obj_set_hidden(gPageName, gHintOn || gNameW < kNameMinW);
  lv_obj_set_hidden(gClock, gHintOn);
  for (int i = 0; i < gItemCount; ++i) lv_obj_set_hidden(gItems[i], gHintOn);
  if (gHintOn) {
    lv_obj_set_style_text_color(gHint, color::accent(), 0);
    widgets::setText(gHint, text);
  }
  for (lv_obj_t* d : gDots) lv_obj_set_style_opa(d, gHintOn ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
}

void setPage(int index, int count, const char* name) {
  // The shown page's palette (pages::show made it current).
  lv_obj_set_style_bg_color(gBar, color::status(), 0);
  lv_obj_set_style_text_color(gClock, color::text(), 0);
  lv_obj_set_style_text_color(gPageName, color::dim(), 0);
  for (int i = 0; i < gItemCount; ++i) lv_obj_set_style_text_color(gItems[i], color::text(), 0);
  widgets::setText(gPageName, name);
  if (count > kMaxDots) count = kMaxDots;
  // At most kWindow dots, sliding so the current page stays inside; a smaller dot at an end means
  // there are more pages that way.
  const int shown = count < gWindow ? count : gWindow;
  int first = index - shown / 2;
  if (first > count - shown) first = count - shown;
  if (first < 0) first = 0;
  const int x0 = screenW() / 2 - 8 - (shown - 1) * kDotSpacing / 2;  // bar has 8 px left padding
  for (int i = 0; i < kMaxDots; ++i) {
    const int slot = i - first;
    if (i >= count || slot < 0 || slot >= shown) {
      lv_obj_set_hidden(gDots[i], true);
      continue;
    }
    const bool edge = (slot == 0 && first > 0) || (slot == shown - 1 && first + shown < count);
    const int size = edge ? 4 : 6;
    lv_obj_set_hidden(gDots[i], false);
    lv_obj_set_size(gDots[i], size, size);
    lv_obj_align(gDots[i], LV_ALIGN_LEFT_MID, x0 + slot * kDotSpacing - size / 2, 0);
    lv_obj_set_style_bg_color(gDots[i], i == index ? color::accent() : color::faint(), 0);
  }
}

void refresh(const Model& m, uint32_t changed) {
  if (changed & kChangedClock) {
    char buf[12] = "--:--";
    if (m.clockMinutes >= 0) snprintf(buf, sizeof(buf), "%02d:%02d", m.clockMinutes / 60, m.clockMinutes % 60);
    widgets::setText(gClock, buf);
  }
  if (changed & kChangedHost) {
    lv_obj_set_style_text_color(gHost, m.hostConnected ? color::ok() : color::faint(), 0);
  }
  const uint32_t itemBits = kChangedClock | kChangedRoom | kChangedWeather | kChangedVolume | kChangedStats | kChangedHost;
  // More items than room (narrow screens): the shown ones move on every 4 s.
  const bool cycling = gItemCount > 0 && gCfgCount > gItemCount;
  if (cycling && millis() - gCycleAt >= kCycleMs) {
    gCycleAt = millis();
    gCycle = (gCycle + 1) % gCfgCount;
    gItemsDirty = true;
  }
  if (gItemCount > 0 && (gItemsDirty || (changed & itemBits))) {
    gItemsDirty = false;
    for (int i = 0; i < gItemCount; ++i) {
      char buf[32];
      itemText(gItemCfg[cycling ? (i + gCycle) % gCfgCount : i], m, buf, sizeof(buf));
      widgets::setText(gItems[i], buf);
    }
  }
}

}  // namespace status_bar
}  // namespace ui
}  // namespace rp
