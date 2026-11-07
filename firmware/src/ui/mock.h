// Demo data for Phase 3 (no host link yet): tracks with non-ASCII titles, wandering system
// stats and a fixed weather report. Media actions act on it. The clock is real (TIME_SYNC).
#pragma once

#include <stdint.h>

#include "model.h"

namespace rp {
namespace ui {
namespace mock {

// Fills the model; returns the Changed bits.
uint32_t begin(Model& m, uint32_t nowMs);

// Advances the mock stats and track (about once per second); returns the Changed bits.
uint32_t update(Model& m, uint32_t nowMs);

// Media actions applied to the mock player.
uint32_t playPause(Model& m, uint32_t nowMs);
uint32_t nextTrack(Model& m, uint32_t nowMs, int dir);
uint32_t seek(Model& m, uint32_t nowMs, int seconds);
uint32_t volume(Model& m, int delta);
uint32_t mute(Model& m);

}  // namespace mock
}  // namespace ui
}  // namespace rp
