// Input actions (docs/config-schema.md, "Actions") and the input-to-action map.
// The device resolves encoder/button gestures into actions. Navigation and display actions run
// locally; media actions go to the host (Phase 5) or out as HID keys (Phase 4). In Phase 3 they
// drive the demo player.
#pragma once

#include <stdint.h>

#include "input/input.h"

namespace rp {
namespace actions {

enum class Action : uint8_t {
  kNone,
  kNavPage,      // rotate
  kNavNext,
  kNavPrev,
  kNavHome,
  kPlayPause,
  kNext,
  kPrev,
  kSeek,         // rotate
  kVolume,       // rotate
  kMute,
  kDisplayToggle,
  kDisplayBrightness,  // rotate
};

const char* name(Action a);
Action parse(const char* s);  // kNone for unknown names

struct EncoderMap {
  Action rotate = Action::kNone;
  Action rotatePressed = Action::kNone;  // turned while the knob is held down
  Action click = Action::kNone;
  Action dbl = Action::kNone;
  Action lng = Action::kNone;
};

struct ButtonMap {
  Action click = Action::kNone;
  Action lng = Action::kNone;
};

struct Map {
  EncoderMap enc[2];
  ButtonMap btn[2];
};

// Default map from docs/config-schema.md.
Map defaults();

void setMap(const Map& m);
const Map& map();

// Resolves an input event to an action and runs it.
void handle(const input::Event& e);

// Runs an action directly (also used by the `action` console command).
void run(Action a, int value);

}  // namespace actions
}  // namespace rp
