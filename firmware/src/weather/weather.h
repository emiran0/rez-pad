// Weather from the host (WEATHER, docs/protocol.md). The link task parses messages in; the UI task
// takes the latest report out. Kept in RAM only: after a reboot without the host there is no
// weather (the device has no clock to judge its age until TIME_SYNC anyway).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ui/model.h"

namespace rp {
namespace weather {

// ---- link task ----
void onWeather(const uint8_t* body, size_t len);

// ---- UI task ----
// True (and `w`, `units` filled) when a new report arrived since the last call.
bool take(ui::Weather& w, ui::Units& units);

uint32_t received();

// Weather is hidden this long after the host fetched it (CLAUDE.md, "Offline behavior").
constexpr int64_t kMaxAgeS = 12 * 3600;

}  // namespace weather
}  // namespace rp
