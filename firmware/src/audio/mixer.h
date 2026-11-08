// The Windows volume mixer from the host (MIXER, docs/protocol.md) and the mixer widget's focus
// (docs/config-schema.md, "Mixer widget"). The link task stores each list; the UI task takes it and
// owns the focus: while focused, knob 1 selects an app, knob 2 sets its volume or mutes it.
// Each mixer widget shows the apps in its own order (its "apps" priority list first); while
// focused, the selection is a position in the focused widget's order.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config_logic.h"
#include "input/input.h"
#include "ui/model.h"

namespace rp {
namespace mixer {

// ---- link task ----
void onMixer(const uint8_t* body, size_t len);

// ---- UI task ----
// The order a widget shows the apps in: the ids in `pins` (comma-joined) first, in that order, then
// the others in the host's order unless `only`. Fills indexes into m.apps; returns how many.
int view(const ui::Mixer& m, const char* pins, bool only, uint8_t (&out)[ui::kMixerApps]);

// Takes a new list into `m` (keeps the selected app selected). True when something changed,
// including a focus timeout.
bool update(ui::Mixer& m, uint32_t nowMs);

// mixer.focus: focuses `widget` (the first mixer widget on the current page, nullptr when there
// is none), or unfocuses. Returns false when there is nothing to focus (no widget, no host, no apps).
bool toggleFocus(ui::Mixer& m, const cfg::WidgetSpec* widget, bool hostConnected, uint32_t nowMs);
void unfocus(ui::Mixer& m);

// While focused: handles knob turns and clicks. True when the event was used (not to be mapped).
bool handleInput(ui::Mixer& m, const input::Event& e, int volumeStep, uint32_t nowMs);

}  // namespace mixer
}  // namespace rp
