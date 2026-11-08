// Audio spectrum from the host (SPECTRUM, docs/protocol.md). The link task stores each frame; the
// UI task takes it into the model, and lets the bars fall by itself when frames stop.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ui/model.h"

namespace rp {
namespace spectrum {

// ---- link task ----
void onSpectrum(const uint8_t* body, size_t len);

// ---- UI task ----
// True when `s` changed: a new frame arrived, or the bars fell after 300 ms without one.
bool update(ui::Spectrum& s, uint32_t nowMs);

uint32_t received();

}  // namespace spectrum
}  // namespace rp
