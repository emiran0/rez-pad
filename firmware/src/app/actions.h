// Input-to-action handling. The action names and the input map types live in the config library
// (lib/config_logic, docs/config-schema.md "Actions"). The device resolves encoder/button gestures
// through the current page's map (global inputs plus that page's inputs_override). Navigation and
// display actions run locally; media actions go to the host, or out as HID keys when it is absent.
#pragma once

#include <stdint.h>

#include "config_logic.h"
#include "input/input.h"

namespace rp {
namespace actions {

// Resolves an input event through the current page's map and runs the action.
void handle(const input::Event& e);

// Runs an action directly (also used by the `action` console command).
void run(Action a, int value);

}  // namespace actions
}  // namespace rp
