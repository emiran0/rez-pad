// Colors, fonts and grid geometry shared by the status bar, pages and widgets.
#pragma once

#include <lvgl.h>

namespace rp {
namespace ui {

// theme.accent from the config; set before the pages are built (ui.cpp).
extern uint32_t gAccentRgb;

namespace color {
inline lv_color_t bg() { return lv_color_hex(0x0E1013); }
inline lv_color_t card() { return lv_color_hex(0x1B1F25); }
inline lv_color_t cardBorder() { return lv_color_hex(0x2A3038); }
inline lv_color_t text() { return lv_color_hex(0xE8EAED); }
inline lv_color_t dim() { return lv_color_hex(0x8A939E); }
inline lv_color_t faint() { return lv_color_hex(0x3A414A); }
inline lv_color_t accent() { return lv_color_hex(gAccentRgb); }
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
