// Demo data until the host sends the real thing: a fixed weather report (Phase 8). Now Playing,
// volume (Phase 6) and system stats (Phase 7) are real.
#pragma once

#include <stdint.h>

#include "model.h"

namespace rp {
namespace ui {
namespace mock {

// Fills the model; returns the Changed bits.
uint32_t begin(Model& m, uint32_t nowMs);

}  // namespace mock
}  // namespace ui
}  // namespace rp
