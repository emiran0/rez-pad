// ILI9488 (320x480, used as 480x320 landscape) on SPI2 via LovyanGFX.
#pragma once

#include <Arduino.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

namespace rp {
namespace display {

constexpr uint32_t kDefaultSpiHz = 40000000;
constexpr uint8_t kDefaultRotation = 1;  // landscape, 480x320

bool begin(uint32_t spiHz = kDefaultSpiHz);
lgfx::LGFX_Device& lcd();

// Requested SPI write clock. The ESP32-S3 SPI clock is 80 MHz / n, so the effective
// clock is the nearest step at or below the request (60 MHz -> 40 MHz).
void setSpiHz(uint32_t hz);
uint32_t spiHz();
uint32_t effectiveSpiHz(uint32_t requestedHz);

void setRotation(uint8_t r);
void setInvert(bool on);

// Orientation, edges, color order and depth test image.
void drawTestPattern();

// Times full-screen fill, a 200x200 region and a full frame in 480x40 strips
// (how LVGL will flush) at each requested clock, then restores the original clock.
void runBenchmark(const uint32_t* hzList, size_t count, Print& out);

}  // namespace display
}  // namespace rp
