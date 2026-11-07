// Demo data until the host sends the real thing: wandering system stats (Phase 7) and a fixed
// weather report (Phase 8). Now Playing and volume are real since Phase 6.
#pragma once

#include <stdint.h>

#include "model.h"

namespace rp {
namespace ui {
namespace mock {

// Fills the model; returns the Changed bits.
uint32_t begin(Model& m, uint32_t nowMs);

// Advances the mock stats (about once per second); returns the Changed bits.
uint32_t update(Model& m, uint32_t nowMs);

}  // namespace mock
}  // namespace ui
}  // namespace rp
