#include "popup.h"

#include <Arduino.h>

#include <initializer_list>

#include "theme.h"
#include "widgets/common.h"
#include "widgets/media_widgets.h"
#include "widgets/weather_icons.h"

namespace rp {
namespace ui {
namespace popup {

namespace {

// 280 x 100 card in the middle of the screen: icon or album art on the left, then the name and
// value, a bar, and a line of detail (times for seek).
constexpr int kW = 280;
constexpr int kH = 100;
constexpr int kIcon = 64;
constexpr int kPad = 14;
constexpr int kColX = kPad + kIcon + 14;
constexpr int kColW = kW - kColX - kPad;
constexpr int kValueW = 56;

lv_obj_t* gRoot = nullptr;
lv_obj_t* gIconBox = nullptr;
lv_obj_t* gSymbol = nullptr;
lv_obj_t* gSun = nullptr;
lv_obj_t* gArtBox = nullptr;
widgets::ArtView gArt;
lv_obj_t* gTitle = nullptr;
lv_obj_t* gValue = nullptr;
lv_obj_t* gBar = nullptr;
lv_obj_t* gLeft = nullptr;
lv_obj_t* gRight = nullptr;

Kind gKind = Kind::kNone;
uint32_t gUntil = 0;
uint8_t gLevel = 0;  // brightness
uint32_t gStyledAccent = 0xFFFFFFFFu;
uint32_t gStyledCard = 0xFFFFFFFFu;

lv_obj_t* plainBox(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_scrollable(b, false);
  lv_obj_set_clickable(b, false);
  return b;
}

// Colors of the shown page (each page has its own palette).
void restyle() {
  const Palette& p = palette();
  if (p.accent == gStyledAccent && p.card == gStyledCard) return;
  gStyledAccent = p.accent;
  gStyledCard = p.card;
  lv_obj_set_style_bg_color(gRoot, color::card(), 0);
  lv_obj_set_style_border_color(gRoot, color::accent(), 0);
  lv_obj_set_style_bg_color(gIconBox, color::faint(), 0);
  lv_obj_set_style_text_color(gSymbol, color::text(), 0);
  lv_obj_set_style_text_color(gTitle, color::text(), 0);
  lv_obj_set_style_text_color(gValue, color::accent(), 0);
  lv_obj_set_style_text_color(gLeft, color::dim(), 0);
  lv_obj_set_style_text_color(gRight, color::dim(), 0);
  lv_obj_set_style_bg_color(gBar, color::faint(), LV_PART_MAIN);
}

void setBar(int32_t max, int32_t value, bool dim) {
  if (lv_bar_get_max_value(gBar) != max) lv_bar_set_range(gBar, 0, max);
  if (value < 0) value = 0;
  if (value > max) value = max;
  if (lv_bar_get_value(gBar) != value) lv_bar_set_value(gBar, value, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(gBar, dim ? color::dim() : color::accent(), LV_PART_INDICATOR);
}

void useArt(bool art) {
  lv_obj_set_hidden(gArtBox, !art);
  lv_obj_set_hidden(gIconBox, art);
}

void fillVolume(const Model& m) {
  useArt(false);
  lv_obj_set_hidden(gSun, true);
  lv_obj_set_hidden(gSymbol, false);
  widgets::setText(gTitle, "Volume");
  widgets::setText(gRight, "");
  if (!m.volume.valid) {
    // Offline the knob sends volume keys; the level itself is unknown.
    widgets::setText(gSymbol, LV_SYMBOL_VOLUME_MAX);
    widgets::setText(gValue, "--");
    widgets::setText(gLeft, m.hostConnected ? "" : "Sent as media keys");
    setBar(100, 0, true);
    return;
  }
  const uint8_t level = m.volume.level;
  const bool muted = m.volume.muted;
  widgets::setText(gSymbol, muted ? LV_SYMBOL_MUTE : (level < 50 ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_VOLUME_MAX));
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", level);
  widgets::setText(gValue, buf);
  widgets::setText(gLeft, muted ? "Muted" : "");
  setBar(100, level, muted);
}

void fillAppVolume(const Model& m, uint32_t nowMs) {
  useArt(true);
  gArt.refresh(m, nowMs);
  widgets::setText(gRight, "");
  if (!m.volume.appValid) {
    widgets::setText(gTitle, "No media app");
    widgets::setText(gValue, "--");
    widgets::setText(gLeft, "Nothing is playing through the mixer");
    setBar(100, 0, true);
    return;
  }
  widgets::setText(gTitle, m.volume.appName);
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", m.volume.appLevel);
  widgets::setText(gValue, buf);
  if (m.volume.appMuted) widgets::setText(gLeft, "Muted");
  else widgets::setText(gLeft, m.np.state == PlayState::kNone ? "App volume" : m.np.title);
  setBar(100, m.volume.appLevel, m.volume.appMuted);
}

void fillSeek(const Model& m, uint32_t nowMs) {
  useArt(true);
  gArt.refresh(m, nowMs);
  widgets::setText(gValue, "");
  const bool none = m.np.state == PlayState::kNone;
  widgets::setText(gTitle, none ? "Nothing playing" : m.np.title);
  if (none || !m.np.canSeek || m.np.durMs == 0) {
    widgets::setText(gLeft, none ? "" : "This app can't seek");
    widgets::setText(gRight, "");
    setBar(1000, 0, true);
    return;
  }
  const uint32_t pos = m.np.positionAt(nowMs);
  char buf[16];
  widgets::fmtDuration(buf, sizeof(buf), pos);
  widgets::setText(gLeft, buf);
  widgets::fmtDuration(buf, sizeof(buf), m.np.durMs);
  widgets::setText(gRight, buf);
  setBar(1000, static_cast<int32_t>(static_cast<uint64_t>(pos) * 1000 / m.np.durMs), false);
}

void fillBrightness() {
  useArt(false);
  lv_obj_set_hidden(gSymbol, true);
  lv_obj_set_hidden(gSun, false);
  widgets::setText(gTitle, "Brightness");
  char buf[8];
  snprintf(buf, sizeof(buf), "%u%%", gLevel);
  widgets::setText(gValue, buf);
  widgets::setText(gLeft, "");
  widgets::setText(gRight, "");
  setBar(100, gLevel, false);
}

bool empty(lv_obj_t* label) {
  const char* t = lv_label_get_text(label);
  return !t || !*t;
}

void setWidth(lv_obj_t* obj, int w) {
  if (lv_obj_get_style_width(obj, LV_PART_MAIN) != w) lv_obj_set_width(obj, w);
}

// The name and the detail line take the whole row when nothing sits on their right.
void fitWidths() {
  setWidth(gTitle, empty(gValue) ? kColW : kColW - kValueW - 4);
  setWidth(gLeft, empty(gRight) ? kColW : kColW - 54);
}

void fill(const Model& m, uint32_t nowMs) {
  switch (gKind) {
    case Kind::kVolume: fillVolume(m); break;
    case Kind::kAppVolume: fillAppVolume(m, nowMs); break;
    case Kind::kSeek: fillSeek(m, nowMs); break;
    case Kind::kBrightness: fillBrightness(); break;
    case Kind::kNone: break;
  }
  fitWidths();
}

const char* kindName(Kind k) {
  switch (k) {
    case Kind::kVolume: return "volume";
    case Kind::kAppVolume: return "app volume";
    case Kind::kSeek: return "seek";
    case Kind::kBrightness: return "brightness";
    default: return "none";
  }
}

uint32_t interests(Kind k) {
  switch (k) {
    case Kind::kVolume: return kChangedVolume | kChangedHost;
    case Kind::kAppVolume: return kChangedVolume | kChangedMedia | kChangedArt | kChangedHost;
    case Kind::kSeek: return kChangedMedia | kChangedArt | kChangedTick | kChangedHost;
    default: return 0;
  }
}

}  // namespace

void build() {
  if (gRoot) return;
  gRoot = plainBox(lv_layer_top(), 0, 0, kW, kH);
  lv_obj_center(gRoot);
  lv_obj_set_style_bg_opa(gRoot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(gRoot, 2, 0);
  lv_obj_set_style_radius(gRoot, 12, 0);

  const int iconY = (kH - kIcon) / 2;
  gIconBox = plainBox(gRoot, kPad, iconY - 2, kIcon, kIcon);
  lv_obj_set_style_bg_opa(gIconBox, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(gIconBox, 10, 0);
  gSymbol = widgets::makeLabel(gIconBox, font::large(), color::text(), LV_SYMBOL_VOLUME_MAX);
  lv_obj_center(gSymbol);
  gSun = lv_image_create(gIconBox);
  lv_image_set_src(gSun, rp_weather_icon(RP_WX_CLEAR_DAY, 48));
  lv_obj_center(gSun);
  lv_obj_set_hidden(gSun, true);

  gArtBox = plainBox(gRoot, kPad, iconY - 2, kIcon, kIcon);
  gArt.build(gArtBox, 0, 0, kIcon, false);

  gTitle = widgets::makeEllipsis(gRoot, font::body(), color::text(), kColW - kValueW - 4);
  lv_obj_set_pos(gTitle, kColX, 12);
  gValue = widgets::makeLabel(gRoot, font::body(), color::accent(), "");
  lv_obj_set_width(gValue, kValueW);
  lv_obj_set_style_text_align(gValue, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(gValue, kColX + kColW - kValueW, 12);

  gBar = lv_bar_create(gRoot);
  lv_obj_set_pos(gBar, kColX, 46);
  lv_obj_set_size(gBar, kColW, 10);
  lv_obj_set_style_anim_duration(gBar, 0, 0);
  lv_bar_set_range(gBar, 0, 100);

  gLeft = widgets::makeEllipsis(gRoot, font::small(), color::dim(), kColW - 50);
  lv_obj_set_pos(gLeft, kColX, 66);
  gRight = widgets::makeLabel(gRoot, font::small(), color::dim(), "");
  lv_obj_set_width(gRight, 50);
  lv_obj_set_style_text_align(gRight, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(gRight, kColX + kColW - 50, 66);

  // One line each: without a fixed height LVGL wraps long text (DOTS only cuts what overflows the
  // height), and wrapped lines run into the bar and each other.
  for (lv_obj_t* l : {gTitle, gValue, gLeft, gRight}) {
    lv_obj_set_height(l, lv_font_get_line_height(lv_obj_get_style_text_font(l, LV_PART_MAIN)));
  }

  lv_obj_set_hidden(gRoot, true);
}

void show(Kind kind, const Model& m, uint32_t nowMs, uint8_t level) {
  if (!gRoot || kind == Kind::kNone) return;
  if (kind != gKind) Serial.printf("[popup] %s\n", kindName(kind));
  gKind = kind;
  gLevel = level;
  gUntil = nowMs + kShowMs;
  restyle();
  fill(m, nowMs);
  lv_obj_set_hidden(gRoot, false);
}

void hide() {
  if (!gRoot || gKind == Kind::kNone) return;
  Serial.printf("[popup] %s hidden\n", kindName(gKind));
  gKind = Kind::kNone;
  lv_obj_set_hidden(gRoot, true);
}

Kind shown() { return gKind; }

void refresh(const Model& m, uint32_t changed, uint32_t nowMs) {
  if (gKind == Kind::kNone) return;
  if (static_cast<int32_t>(nowMs - gUntil) >= 0) {
    hide();
    return;
  }
  // App volume and seek are host-only actions: nothing to show once the host is gone.
  if ((changed & kChangedHost) && !m.hostConnected && (gKind == Kind::kAppVolume || gKind == Kind::kSeek)) {
    hide();
    return;
  }
  restyle();  // the page (and its palette) may have changed
  if (changed & interests(gKind)) fill(m, nowMs);
}

}  // namespace popup
}  // namespace ui
}  // namespace rp
