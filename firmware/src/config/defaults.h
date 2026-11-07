// Built-in defaults until the config store exists (Phase 9). Values mirror the default
// config in docs/config-schema.md.
#pragma once

#include <stdint.h>

namespace rp {
namespace defaults {

constexpr int kCountsPerDetent = 4;  // measured on the fitted KY-040 (docs/hardware.md)
constexpr bool kEncInvert = false;
constexpr uint8_t kButtons = 0;      // physical buttons fitted: 0..2

constexpr float kTempOffsetC = -1.5f;
constexpr float kHumOffsetPct = 0.0f;
constexpr float kTrendThresholdHpa = 1.0f;

constexpr uint8_t kBrightness = 80;

}  // namespace defaults
}  // namespace rp
