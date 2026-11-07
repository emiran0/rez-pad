#include "backlight.h"

#include <Arduino.h>
#include <math.h>

#include "pins.h"

namespace rp {
namespace backlight {

namespace {

constexpr uint32_t kPwmHz = 20000;  // above audible range, no visible flicker
constexpr uint8_t kPwmBits = 10;
constexpr uint32_t kMaxDuty = (1u << kPwmBits) - 1;
constexpr float kGamma = 2.2f;

uint8_t gPercent = 0;
bool gReady = false;

}  // namespace

void begin() {
  gReady = ledcAttach(pins::kTftBacklight, kPwmHz, kPwmBits);
  if (!gReady) {
    // Fall back to plain on/off so the screen is still visible.
    pinMode(pins::kTftBacklight, OUTPUT);
    digitalWrite(pins::kTftBacklight, LOW);
  } else {
    ledcWrite(pins::kTftBacklight, 0);
  }
  gPercent = 0;
}

void set(uint8_t percent) {
  if (percent > 100) percent = 100;
  gPercent = percent;
  if (!gReady) {
    digitalWrite(pins::kTftBacklight, percent > 0 ? HIGH : LOW);
    return;
  }
  uint32_t duty = 0;
  if (percent > 0) {
    duty = static_cast<uint32_t>(lroundf(powf(percent / 100.0f, kGamma) * kMaxDuty));
    if (duty == 0) duty = 1;  // keep the lowest step visible
  }
  ledcWrite(pins::kTftBacklight, duty);
}

uint8_t get() { return gPercent; }

}  // namespace backlight
}  // namespace rp
