// Knob popup: a small card in the middle of the screen that shows what a knob action is changing
// (PC volume, the Now Playing app's volume, the track position, screen brightness) when the shown
// page has no widget for it. It lives on LVGL's top layer, so it stays up across page switches,
// and hides 3 s after the last change. UI task only.
#pragma once

#include <stdint.h>

#include "model.h"

namespace rp {
namespace ui {
namespace popup {

enum class Kind : uint8_t { kNone, kVolume, kAppVolume, kSeek, kBrightness };

constexpr uint32_t kShowMs = 3000;

// Creates the (hidden) popup once, after the screen exists.
void build();

// Shows `kind` with the model's current values (brightness: `level`), or keeps it up, and restarts
// the 3 s timer.
void show(Kind kind, const Model& m, uint32_t nowMs, uint8_t level = 0);
void hide();
Kind shown();

// Every UI loop: live values while shown, and the timeout.
void refresh(const Model& m, uint32_t changed, uint32_t nowMs);

}  // namespace popup
}  // namespace ui
}  // namespace rp
