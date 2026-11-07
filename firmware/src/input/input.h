// Encoders (PCNT quadrature counting) and switches/buttons (1 ms sampling, debounce, gestures).
// Produces input events on a queue; the UI loop drains it with poll().
#pragma once

#include <stdint.h>

#include "input_logic.h"

namespace rp {
namespace input {

enum class Source : uint8_t { kEnc1, kEnc2, kBtn1, kBtn2 };
enum class Kind : uint8_t { kRotate, kClick, kDouble, kLong };

struct Event {
  Source source;
  Kind kind;
  int16_t value;   // detents for kRotate (+ clockwise), 0 otherwise
  uint32_t timeMs;
};

const char* sourceName(Source s);
const char* kindName(Kind k);

struct EncoderConfig {
  int countsPerDetent = 2;
  bool invert = false;
  bool doubleEnabled = true;
};

struct Config {
  EncoderConfig enc[2];
  uint8_t buttons = 0;  // 0..2 fitted
  bool btnDoubleEnabled = false;
};

bool begin(const Config& cfg);
void setConfig(const Config& cfg);
const Config& config();

// Next pending event, if any. Call from the UI loop.
bool poll(Event& e);

struct Snapshot {
  int32_t rawCount[2];   // accumulated PCNT counts
  int32_t detents[2];    // total detents since boot (after invert)
  bool switchDown[2];
  bool buttonDown[2];
  uint32_t dropped;      // events lost because the queue was full
};
Snapshot snapshot();

}  // namespace input
}  // namespace rp
