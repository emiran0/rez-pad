#include "input_logic.h"

namespace rp {
namespace input {

Gesture GestureDetector::update(bool rawPressed, uint32_t nowMs) {
  if (rawPressed != raw_) {
    raw_ = rawPressed;
    rawChangedMs_ = nowMs;
  }

  Gesture out = Gesture::kNone;

  // Debounced edge.
  if (raw_ != stable_ && nowMs - rawChangedMs_ >= cfg_.debounceMs) {
    stable_ = raw_;
    if (stable_) {
      pressedMs_ = nowMs;
      longFired_ = false;
      secondPress_ = clickPending_;  // pressed again inside the double-click window
      clickPending_ = false;
    } else if (!longFired_) {
      if (secondPress_) {
        out = Gesture::kDouble;
        secondPress_ = false;
      } else if (cfg_.doubleEnabled) {
        clickPending_ = true;
        releasedMs_ = nowMs;
      } else {
        out = Gesture::kClick;
      }
    } else {
      secondPress_ = false;
    }
  }

  // Long press fires while held.
  if (stable_ && !longFired_ && nowMs - pressedMs_ >= cfg_.longMs) {
    longFired_ = true;
    // Click then press-and-hold: the hold wins, the first click is dropped.
    secondPress_ = false;
    if (out == Gesture::kNone) out = Gesture::kLong;
  }

  // Single click confirmed once the double-click window has passed.
  if (clickPending_ && !stable_ && nowMs - releasedMs_ > cfg_.doubleMs) {
    clickPending_ = false;
    if (out == Gesture::kNone) out = Gesture::kClick;
  }

  return out;
}

void GestureDetector::cancelPress() {
  if (!stable_) return;
  longFired_ = true;  // suppresses long now and click/double on release
  secondPress_ = false;
  clickPending_ = false;
}

void DetentAccumulator::configure(int countsPerDetent, bool invert) {
  cpd_ = countsPerDetent > 0 ? countsPerDetent : 1;
  invert_ = invert;
}

int DetentAccumulator::update(int32_t count) {
  const int32_t delta = count - base_;
  const int32_t steps = delta / cpd_;  // truncates toward zero, symmetric for both directions
  base_ += steps * cpd_;
  return static_cast<int>(invert_ ? -steps : steps);
}

}  // namespace input
}  // namespace rp
