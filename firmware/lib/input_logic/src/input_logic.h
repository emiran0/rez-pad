// Hardware-independent input logic: switch debouncing, gestures and encoder detent scaling.
// Pure C++, unit-tested on the host (test/test_input).
#pragma once

#include <stdint.h>

namespace rp {
namespace input {

enum class Gesture : uint8_t { kNone, kClick, kDouble, kLong };

struct GestureConfig {
  uint16_t debounceMs = 20;
  uint16_t doubleMs = 300;  // max gap between the first release and the second press
  uint16_t longMs = 600;    // hold time that fires a long press (fires while still held)
  // When no action is mapped to `double`, clicks fire on release instead of waiting doubleMs.
  bool doubleEnabled = true;
};

// Feed the raw (undebounced) switch state on every tick, about every 1 ms.
class GestureDetector {
 public:
  explicit GestureDetector(const GestureConfig& cfg = GestureConfig()) : cfg_(cfg) {}

  void setConfig(const GestureConfig& cfg) { cfg_ = cfg; }
  const GestureConfig& config() const { return cfg_; }

  // Returns a gesture when one completes, otherwise kNone.
  Gesture update(bool rawPressed, uint32_t nowMs);

  bool pressed() const { return stable_; }

  // The current press was used for something else (press-and-turn): it produces no
  // click, double or long, and does not start a double-click window.
  void cancelPress();

 private:
  GestureConfig cfg_;
  bool raw_ = false;
  bool stable_ = false;
  uint32_t rawChangedMs_ = 0;
  uint32_t pressedMs_ = 0;
  uint32_t releasedMs_ = 0;
  bool longFired_ = false;
  bool clickPending_ = false;
  bool secondPress_ = false;
};

// Turns an absolute quadrature count into whole detent steps, keeping the remainder.
class DetentAccumulator {
 public:
  void configure(int countsPerDetent, bool invert);
  void reset(int32_t count) { base_ = count; }

  // Returns the detents moved since the last call (positive = clockwise, unless inverted).
  int update(int32_t count);

  // Call once the count has stopped changing (the knob rests in a click). A remainder of one count
  // (a contact bounce or a nudge) is dropped, and a click one count short (a missed edge) is
  // completed, so the next turn lines up with the clicks again. Half a click is left alone (the
  // knob may be held between clicks). Returns the detents emitted (0 or +-1).
  int settle(int32_t count);

  int32_t pending(int32_t count) const { return count - base_; }

 private:
  int cpd_ = 2;
  bool invert_ = false;
  int32_t base_ = 0;
};

}  // namespace input
}  // namespace rp
