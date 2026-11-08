#include "pages.h"

#include <Arduino.h>

#include "media/media.h"
#include "theme.h"
#include "widgets/common.h"

namespace rp {
namespace ui {
namespace pages {

namespace {

constexpr int kMaxWidgets = cfg::kMaxWidgets;

struct Page {
  const cfg::Page* spec = nullptr;
  Palette pal{};
  lv_obj_t* root = nullptr;
  widgets::Widget* widgets[kMaxWidgets] = {};
  int widgetCount = 0;
};

Page gPages[kMaxPages];
int gCount = 0;
int gCurrent = 0;

lv_obj_t* makeCard(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, w, h);
  lv_obj_set_style_bg_color(card, color::card(), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, color::cardBorder(), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, 8, 0);
  lv_obj_set_style_pad_all(card, 7, 0);
  lv_obj_set_scrollable(card, false);
  lv_obj_set_clickable(card, false);
  return card;
}

void errorCard(lv_obj_t* card, const char* type, const char* why) {
  lv_obj_set_style_border_color(card, color::crit(), 0);
  char buf[64];
  snprintf(buf, sizeof(buf), "%s\n%s", type, why);
  lv_obj_t* l = widgets::makeLabel(card, font::small(), color::crit(), buf);
  lv_obj_center(l);
  Serial.printf("[ui] widget %s: %s\n", type, why);
}

}  // namespace

void build(lv_obj_t* parent, const cfg::Config& cfg) {
  gCount = cfg.pageCount > kMaxPages ? kMaxPages : cfg.pageCount;

  for (int p = 0; p < gCount; ++p) {
    Page& page = gPages[p];
    page.spec = &cfg.pages[p];
    page.widgetCount = 0;
    // Each page has its own grid and palette; widgets read the current palette while building.
    const int cellW = kScreenW / page.spec->cols;
    const int cellH = kGridH / page.spec->rows;
    page.pal = makePalette(page.spec->theme, page.spec->accent);
    setPalette(page.pal);
    page.root = lv_obj_create(parent);
    lv_obj_remove_style_all(page.root);
    lv_obj_set_size(page.root, kScreenW, kGridH);
    lv_obj_set_pos(page.root, 0, 0);
    lv_obj_set_scrollable(page.root, false);
    lv_obj_set_hidden(page.root, true);

    // Position, size and overlap were checked by the config validator (lib/config_logic).
    for (int i = 0; i < page.spec->widgetCount && i < kMaxWidgets; ++i) {
      const widgets::Spec& ws = page.spec->widgets[i];
      const int x = ws.x * cellW + kCellGap / 2;
      const int y = ws.y * cellH + kCellGap / 2;
      const int w = ws.w * cellW - kCellGap;
      const int h = ws.h * cellH - kCellGap;
      lv_obj_t* card = makeCard(page.root, x, y, w, h);
      widgets::Widget* widget = widgets::create(ws);
      if (!widget) {
        errorCard(card, ws.type, "not supported");
        continue;
      }
      if (widget->fullBleed()) {
        // Edge to edge inside the 1 px border (images), clipped to the rounded corners.
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_set_style_clip_corner(card, true, 0);
        widget->build(card, w - 2, h - 2);
      } else {
        widget->build(card, w - 14, h - 14);  // content area inside the 7 px padding
      }
      page.widgets[page.widgetCount++] = widget;
    }
  }
  gCurrent = 0;
  if (gCount > 0) {
    lv_obj_set_hidden(gPages[0].root, false);
    setPalette(gPages[0].pal);
    lv_obj_set_style_bg_color(lv_screen_active(), color::bg(), 0);
  }
}

void clear() {
  for (int p = 0; p < gCount; ++p) {
    Page& page = gPages[p];
    for (int i = 0; i < page.widgetCount; ++i) delete page.widgets[i];
    page.widgetCount = 0;
    if (page.root) lv_obj_delete(page.root);
    page.root = nullptr;
    page.spec = nullptr;
  }
  gCount = 0;
  gCurrent = 0;
  media::setArtSize(0);  // album_art sets it again if the new layout has one
}

int count() { return gCount; }
int current() { return gCurrent; }
const char* name(int index) { return (index >= 0 && index < gCount) ? gPages[index].spec->name : ""; }

void show(int index) {
  if (gCount == 0) return;
  if (index < 0 || index >= gCount) index = 0;
  if (index != gCurrent) lv_obj_set_hidden(gPages[gCurrent].root, true);
  gCurrent = index;
  lv_obj_set_hidden(gPages[gCurrent].root, false);
  setPalette(gPages[gCurrent].pal);
  lv_obj_set_style_bg_color(lv_screen_active(), color::bg(), 0);
}

void step(int delta) {
  if (gCount == 0) return;
  int next = (gCurrent + delta) % gCount;
  if (next < 0) next += gCount;
  show(next);
}

void refresh(const Model& m, uint32_t changed, uint32_t nowMs) {
  for (int p = 0; p < gCount; ++p) {
    setPalette(gPages[p].pal);
    for (int i = 0; i < gPages[p].widgetCount; ++i) {
      widgets::Widget* w = gPages[p].widgets[i];
      const uint32_t mine = w->interests() & changed;
      if (!mine) continue;
      // Hidden pages only need state that accumulates (graph history); skip the per-tick work and
      // the spectrum.
      if (p != gCurrent && (mine & ~(kChangedTick | kChangedSpectrum)) == 0) continue;
      w->refresh(m, mine, nowMs);
    }
  }
  if (gCount > 0) setPalette(gPages[gCurrent].pal);  // the status bar uses the shown page's colors
}

}  // namespace pages
}  // namespace ui
}  // namespace rp
