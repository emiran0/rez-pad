// UI orchestration: builds the LVGL screen (status bar + pages) and keeps it in sync with the
// model. Runs in the UI loop only (all LVGL calls stay on this task).
#pragma once

#include <stdint.h>

#include "app/actions.h"

namespace rp {
namespace ui {

bool begin();

// Call every loop iteration: feeds sensor data, runs demo data, refreshes widgets, runs LVGL.
void loop(uint32_t nowMs);

void showPage(int index);

// Mixer widget (docs/config-schema.md, "Mixer widget"): mixer.focus, and knob input while focused
// (returns true when the mixer used the event).
void toggleMixerFocus();
bool mixerInput(const input::Event& e);
void stepPage(int delta);

// Media actions; in Phase 3 they drive the demo player.
void mediaAction(actions::Action a, int value);

}  // namespace ui
}  // namespace rp
