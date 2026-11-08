// Colors, fonts and grid geometry shared by the status bar, pages and widgets.
#pragma once

#include <lvgl.h>

namespace rp {
namespace ui {

// Theme presets (docs/config-schema.md, "Themes"): screen, card and text colors plus an accent.
// Each page has its own palette; the page manager makes it current while that page builds and
// refreshes, so widgets just ask color:: for the current one.
struct Palette {
  uint32_t bg, card, border, text, dim, faint, status, accent;
};

// Preset `theme` (cfg::themeName order) with an accent color.
Palette makePalette(int theme, uint32_t accent);
const Palette& palette();
void setPalette(const Palette& p);

namespace color {
inline lv_color_t bg() { return lv_color_hex(palette().bg); }
inline lv_color_t card() { return lv_color_hex(palette().card); }
inline lv_color_t cardBorder() { return lv_color_hex(palette().border); }
inline lv_color_t text() { return lv_color_hex(palette().text); }
inline lv_color_t dim() { return lv_color_hex(palette().dim); }
inline lv_color_t faint() { return lv_color_hex(palette().faint); }
inline lv_color_t status() { return lv_color_hex(palette().status); }
inline lv_color_t accent() { return lv_color_hex(palette().accent); }
inline lv_color_t ok() { return lv_color_hex(0x66BB6A); }
inline lv_color_t warn() { return lv_color_hex(0xFFB300); }
inline lv_color_t crit() { return lv_color_hex(0xEF5350); }
}  // namespace color

namespace font {
inline const lv_font_t* small() { return &rp_font_14; }
inline const lv_font_t* body() { return &rp_font_20; }
inline const lv_font_t* large() { return &rp_font_28; }
inline const lv_font_t* huge() { return &rp_font_48; }  // ASCII + degree sign only
}  // namespace font

// Screen layout: 20 px status bar, grid below it.
constexpr int kScreenW = 480;
constexpr int kScreenH = 320;
constexpr int kStatusH = 20;
constexpr int kGridY = kStatusH;
constexpr int kGridH = kScreenH - kStatusH;
constexpr int kCellGap = 4;  // space between cards

}  // namespace ui
}  // namespace rp
