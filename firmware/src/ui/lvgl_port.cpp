#include "lvgl_port.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "display/display.h"
#include "theme.h"

namespace rp {
namespace lvgl_port {

namespace {

constexpr int kBufLines = 40;
lv_display_t* gDisp = nullptr;
bool gPaused = false;

uint32_t tick() { return millis(); }

void flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
  const int32_t w = lv_area_get_width(area);
  const int32_t h = lv_area_get_height(area);
  // LVGL RGB565 is native-endian; rgb565_t tells LovyanGFX not to byte-swap. LovyanGFX converts
  // to RGB666 into its own DMA buffers, so `px` can be reused as soon as this call returns.
  display::lcd().pushImageDMA(area->x1, area->y1, w, h, reinterpret_cast<const lgfx::rgb565_t*>(px));
  lv_display_flush_ready(disp);
}

void logCb(lv_log_level_t, const char* buf) { Serial.print(buf); }

}  // namespace

bool begin() {
  lv_init();
  lv_tick_set_cb(tick);
  lv_log_register_print_cb(logCb);

  auto& lcd = display::lcd();
  gDisp = lv_display_create(lcd.width(), lcd.height());
  if (!gDisp) return false;
  lv_display_set_color_format(gDisp, LV_COLOR_FORMAT_RGB565);

  const size_t bytes = static_cast<size_t>(lcd.width()) * kBufLines * 2;
  void* b1 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void* b2 = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!b1 || !b2) return false;
  lv_display_set_buffers(gDisp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(gDisp, flush);
  lv_sysmon_hide_performance(gDisp);  // compiled in, shown only with the `perf 1` console command

  // Keep the SPI bus claimed for the whole session: the display is alone on SPI2, and this
  // lets the next strip render while the previous one is still being sent.
  lcd.startWrite();

  lv_theme_t* th = lv_theme_default_init(gDisp, ui::color::accent(), ui::color::warn(), true, ui::font::body());
  lv_display_set_theme(gDisp, th);
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, ui::color::bg(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(scr, false);
  return true;
}

uint32_t handle() {
  if (gPaused) return 20;
  return lv_timer_handler();
}

void setPaused(bool paused) {
  if (gPaused == paused) return;
  gPaused = paused;
  if (!paused) invalidateAll();
}

bool paused() { return gPaused; }

void invalidateAll() {
  if (gDisp) lv_obj_invalidate(lv_display_get_screen_active(gDisp));
}

void showPerfMonitor(bool on) {
  if (!gDisp) return;
  if (on) lv_sysmon_show_performance(gDisp);
  else lv_sysmon_hide_performance(gDisp);
}

}  // namespace lvgl_port
}  // namespace rp
