// Display backlight on LEDC PWM (pins::kTftBacklight, active high via the module's transistor Q1).
#pragma once

#include <stdint.h>

namespace rp {
namespace backlight {

void begin();

// 0 to 100 (perceptual: gamma-corrected so 50 looks about half as bright as 100).
void set(uint8_t percent);
uint8_t get();

}  // namespace backlight
}  // namespace rp
