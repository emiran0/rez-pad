// Encoders (PCNT quadrature counting) and switches/buttons (1 ms sampling, debounce, gestures).
// Produces input events on a queue; the UI loop drains it with poll().
//
// Physical knob ports 1 to 3 and button ports 1 to 4 (pins.h) are always sampled. The config's
// numbering (knobPorts / buttonPorts) says which knob or button number each port is; events
// carry the number (Source), ports without a number produce none. Learn mode reports the next
// port that is turned or pressed instead (numbering from the web page).
#pragma once

#include <stdint.h>

#include "input_logic.h"

namespace rp {
namespace input {

// Knob and button numbers (enc1..enc3, btn1..btn4 in the config).
enum class Source : uint8_t { kEnc1, kEnc2, kEnc3, kBtn1, kBtn2, kBtn3, kBtn4 };

constexpr int kMaxKnobs = 3;    // knob numbers, and physical knob ports
constexpr int kMaxButtons = 4;  // button numbers, and physical button ports
// kRotatePressed: turned while the encoder's own switch is held (press-and-turn).
enum class Kind : uint8_t { kRotate, kRotatePressed, kClick, kDouble, kLong };

struct Event {
  Source source;
  Kind kind;
  int16_t value;   // detents for kRotate / kRotatePressed (+ clockwise), 0 otherwise
  uint32_t timeMs;
};

const char* sourceName(Source s);
const char* kindName(Kind k);
inline bool isKnob(Source s) { return s <= Source::kEnc3; }
inline int knobIndex(Source s) { return static_cast<int>(s); }  // 0-based knob number
inline int buttonIndex(Source s) { return static_cast<int>(s) - static_cast<int>(Source::kBtn1); }

struct EncoderConfig {
  int countsPerDetent = 4;
  bool invert = false;
  bool doubleEnabled = true;
};

struct Config {
  EncoderConfig enc[kMaxKnobs];             // per knob number
  uint8_t knobPorts[kMaxKnobs] = {1, 2, 0};  // knob number -> port (1-based), 0 = not used
  uint8_t buttonPorts[kMaxButtons] = {};     // button number -> port, 0 = not used
  bool btnDoubleEnabled[kMaxButtons] = {};   // only when a double click is mapped (else clicks fire at once)
};

bool begin(const Config& cfg);
void setConfig(const Config& cfg);
const Config& config();

// Next pending event, if any. Call from the UI loop.
bool poll(Event& e);

// Learn mode: the next knob port turned or pressed, or button port pressed, is reported through
// takeLearned() instead of acting (ports with and without a number alike). Ends with that
// control, after timeoutMs (reported as port 0), or with startLearn(0). Inputs stay quiet for a
// moment afterwards, so the rest of that turn or press does nothing.
void startLearn(uint32_t timeoutMs);
bool learning();
struct Learned {
  bool knob = false;
  uint8_t port = 0;  // 1-based; 0 = timed out or cancelled
};
bool takeLearned(Learned& out);  // true once per learn session, when it ended

struct Snapshot {  // by physical port
  int32_t rawCount[kMaxKnobs];   // accumulated PCNT counts
  int32_t detents[kMaxKnobs];    // total detents since boot (after invert)
  bool switchDown[kMaxKnobs];
  bool buttonDown[kMaxButtons];
  uint32_t dropped;      // events lost because the queue was full
};
Snapshot snapshot();

// Debug trace of encoder count changes (console `enclog`).
struct TraceEntry {
  uint32_t ms;
  uint8_t port;   // 0-based
  uint8_t level;  // bit 0 = A, bit 1 = B, bit 2 = re-aligned at rest (count unchanged)
  int8_t steps;   // detents emitted at this change
  int32_t count;
};
// Copies up to max entries (oldest first) and clears the trace. Returns the number copied.
int takeTrace(TraceEntry* out, int max);

}  // namespace input
}  // namespace rp
