#include "status_bar.h"

#include "theme.h"
#include "widgets/common.h"

namespace rp {
namespace ui {
namespace status_bar {

namespace {

constexpr int kMaxDots = 8;
lv_obj_t* gBar = nullptr;
lv_obj_t* gClock = nullptr;
lv_obj_t* gPageName = nullptr;
lv_obj_t* gDots[kMaxDots] = {};
lv_obj_t* gHost = nullptr;
lv_obj_t* gWarn = nullptr;

}  // namespace

void build(lv_obj_t* parent) {
  gBar = lv_obj_create(parent);
  lv_obj_remove_style_all(gBar);
  lv_obj_set_size(gBar, kScreenW, kStatusH);
  lv_obj_set_pos(gBar, 0, 0);
  lv_obj_set_style_bg_color(gBar, lv_color_hex(0x15181D), 0);
  lv_obj_set_style_bg_opa(gBar, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(gBar, 8, 0);
  lv_obj_set_scrollable(gBar, false);

  gClock = widgets::makeLabel(gBar, font::small(), color::text(), "--:--");
  lv_obj_align(gClock, LV_ALIGN_LEFT_MID, 0, 0);

  gPageName = widgets::makeLabel(gBar, font::small(), color::dim(), "");
  lv_obj_align(gPageName, LV_ALIGN_LEFT_MID, 56, 0);

  for (int i = 0; i < kMaxDots; ++i) {
    gDots[i] = lv_obj_create(gBar);
    lv_obj_remove_style_all(gDots[i]);
    lv_obj_set_size(gDots[i], 6, 6);
    lv_obj_set_style_radius(gDots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(gDots[i], LV_OPA_COVER, 0);
    lv_obj_set_hidden(gDots[i], true);
  }

  gHost = widgets::makeLabel(gBar, font::small(), color::faint(), LV_SYMBOL_USB);
  lv_obj_align(gHost, LV_ALIGN_RIGHT_MID, 0, 0);

  gWarn = widgets::makeLabel(gBar, font::small(), color::warn(), LV_SYMBOL_WARNING " config");
  lv_obj_align(gWarn, LV_ALIGN_RIGHT_MID, -22, 0);
  lv_obj_set_hidden(gWarn, true);
}

void setWarning(const char* text) {
  // The bar has no room for the reason; it is in the serial log and the host's status page.
  lv_obj_set_hidden(gWarn, !(text && text[0]));
}

void setPage(int index, int count, const char* name) {
  widgets::setText(gPageName, name);
  if (count > kMaxDots) count = kMaxDots;
  const int spacing = 12;
  const int x0 = kScreenW / 2 - 8 - (count - 1) * spacing / 2;  // bar has 8 px left padding
  for (int i = 0; i < kMaxDots; ++i) {
    if (i >= count) {
      lv_obj_set_hidden(gDots[i], true);
      continue;
    }
    lv_obj_set_hidden(gDots[i], false);
    lv_obj_align(gDots[i], LV_ALIGN_LEFT_MID, x0 + i * spacing - 3, 0);
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
}

}  // namespace status_bar
}  // namespace ui
}  // namespace rp
