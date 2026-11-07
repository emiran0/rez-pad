// System stats from the host (SYS_STATS, docs/protocol.md). The link task parses messages in;
// the UI task takes the latest set out. Only the sources the layout uses are sent.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ui/model.h"

namespace rp {
namespace sysstats {

// ---- link task ----
void onSysStats(const uint8_t* body, size_t len);

// ---- UI task ----
// True (and `out` filled, ui::kMaxStats entries) when a new set arrived since the last call.
bool take(ui::Stat (&out)[ui::kMaxStats]);

uint32_t received();
uint32_t dropped();  // sources beyond ui::kMaxStats, or ids too long

}  // namespace sysstats
}  // namespace rp
