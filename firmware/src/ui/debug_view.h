// Phase 2 on-screen debug view: encoder counts, gesture log and sensor readings.
// Plain LovyanGFX text, replaced by LVGL pages in Phase 3. Call only from the UI loop.
#pragma once

#include "input/input.h"
#include "sensors/sensors.h"

namespace rp {
namespace debug_view {

// Clears the screen and draws the static layout.
void show();
bool active();
void hide();

void addEvent(const input::Event& e);

// Redraws only fields whose text changed. Cheap enough to call at 20 Hz.
void update(const input::Snapshot& in, const sensors::Reading& r);

}  // namespace debug_view
}  // namespace rp
