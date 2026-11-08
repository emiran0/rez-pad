// LVGL 9 on the ILI9488 through LovyanGFX: display driver, tick and log hookup.
#pragma once

#include <lvgl.h>

namespace rp {
namespace lvgl_port {

// Creates the LVGL display with two 480x40 RGB565 draw buffers in internal DMA RAM.
bool begin();

// Runs LVGL timers and rendering. Returns ms until LVGL wants to run again.
uint32_t handle();

// While paused (debug view, test pattern), LVGL does not render or flush.
void setPaused(bool paused);
// True while paused or sleeping.
bool paused();

// Backlight off (display/screen.cpp): no rendering, independent of setPaused().
void setSleeping(bool sleeping);
bool sleeping();

// Renders everything that is invalid right now (instead of on LVGL's next refresh timer).
void refreshNow();

// Forces a full redraw on the next handle().
void invalidateAll();

void showPerfMonitor(bool on);

// Totals since boot: rendered frames, flushed strips and pixels, time spent pushing them (SPI).
struct RenderStats {
  uint32_t frames, flushes, flushMs;
  uint64_t pixels;
};
RenderStats renderStats();

}  // namespace lvgl_port
}  // namespace rp
