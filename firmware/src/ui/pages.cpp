#include "pages.h"

#include <Arduino.h>

#include "theme.h"
#include "widgets/common.h"

namespace rp {
namespace ui {
namespace pages {

namespace {

constexpr int kMaxWidgets = 16;

struct Page {
  const PageSpec* spec = nullptr;
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

void build(lv_obj_t* parent, const PageSpec* specs, int count, int cols, int rows) {
  gCount = count > kMaxPages ? kMaxPages : count;
  if (cols < 1 || cols > 8) cols = 4;  // the occupancy map below is 8x8
  if (rows < 1 || rows > 8) rows = 3;
  const int cellW = kScreenW / cols;
  const int cellH = kGridH / rows;

  for (int p = 0; p < gCount; ++p) {
    Page& page = gPages[p];
    page.spec = &specs[p];
    page.root = lv_obj_create(parent);
    lv_obj_remove_style_all(page.root);
    lv_obj_set_size(page.root, kScreenW, kGridH);
    lv_obj_set_pos(page.root, 0, 0);
    lv_obj_set_scrollable(page.root, false);
    lv_obj_set_hidden(page.root, true);

    bool used[8][8] = {};
    for (int i = 0; i < specs[p].count && page.widgetCount < kMaxWidgets; ++i) {
      const widgets::Spec& ws = specs[p].widgets[i];
      const int x = ws.x * cellW + kCellGap / 2;
      const int y = ws.y * cellH + kCellGap / 2;
      const int w = ws.w * cellW - kCellGap;
      const int h = ws.h * cellH - kCellGap;
      lv_obj_t* card = makeCard(page.root, x, y, w, h);

      if (ws.x + ws.w > cols || ws.y + ws.h > rows || ws.w == 0 || ws.h == 0) {
        errorCard(card, ws.type, "outside grid");
        continue;
      }
      bool overlap = false;
      for (int cy = ws.y; cy < ws.y + ws.h; ++cy)
        for (int cx = ws.x; cx < ws.x + ws.w; ++cx) overlap = overlap || used[cy][cx];
      if (overlap) {
        errorCard(card, ws.type, "overlaps");
        continue;
      }
      for (int cy = ws.y; cy < ws.y + ws.h; ++cy)
        for (int cx = ws.x; cx < ws.x + ws.w; ++cx) used[cy][cx] = true;
      if (!widgets::sizeAllowed(ws.type, ws.w, ws.h)) {
        errorCard(card, ws.type, "size not allowed");
        continue;
      }
      widgets::Widget* widget = widgets::create(ws);
      if (!widget) {
        errorCard(card, ws.type, "not implemented yet");
        continue;
      }
      widget->build(card, w - 14, h - 14);  // content area inside the 7 px padding
      page.widgets[page.widgetCount++] = widget;
    }
  }
  if (gCount > 0) show(0);
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
}

void step(int delta) {
  if (gCount == 0) return;
  int next = (gCurrent + delta) % gCount;
  if (next < 0) next += gCount;
  show(next);
}

void refresh(const Model& m, uint32_t changed, uint32_t nowMs) {
  for (int p = 0; p < gCount; ++p) {
    for (int i = 0; i < gPages[p].widgetCount; ++i) {
      widgets::Widget* w = gPages[p].widgets[i];
      const uint32_t mine = w->interests() & changed;
      if (!mine) continue;
      // Hidden pages only need state that accumulates (graph history); skip the per-tick work.
      if (p != gCurrent && mine == kChangedTick) continue;
      w->refresh(m, mine, nowMs);
    }
  }
}

}  // namespace pages
}  // namespace ui
}  // namespace rp
