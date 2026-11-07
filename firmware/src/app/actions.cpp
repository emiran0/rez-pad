#include "actions.h"

#include <Arduino.h>
#include <string.h>

#include "config/defaults.h"
#include "display/backlight.h"
#include "hid/media_keys.h"
#include "link/link.h"
#include "ui/ui.h"

namespace rp {
namespace actions {

namespace {

Map gMap = defaults();
bool gScreenOff = false;
uint8_t gBrightness = rp::defaults::kBrightness;

struct Name {
  Action a;
  const char* s;
};
const Name kNames[] = {
    {Action::kNone, "none"},
    {Action::kNavPage, "nav.page"},
    {Action::kNavNext, "nav.next_page"},
    {Action::kNavPrev, "nav.prev_page"},
    {Action::kNavHome, "nav.home"},
    {Action::kPlayPause, "media.play_pause"},
    {Action::kNext, "media.next"},
    {Action::kPrev, "media.prev"},
    {Action::kSeek, "media.seek"},
    {Action::kVolume, "media.volume"},
    {Action::kMute, "media.mute"},
    {Action::kDisplayToggle, "display.toggle"},
    {Action::kDisplayBrightness, "display.brightness"},
};

Action resolve(const input::Event& e) {
  const int idx = (e.source == input::Source::kEnc1 || e.source == input::Source::kBtn1) ? 0 : 1;
  const bool enc = e.source == input::Source::kEnc1 || e.source == input::Source::kEnc2;
  if (enc) {
    const EncoderMap& m = gMap.enc[idx];
    switch (e.kind) {
      case input::Kind::kRotate: return m.rotate;
      case input::Kind::kRotatePressed: return m.rotatePressed;
      case input::Kind::kClick: return m.click;
      case input::Kind::kDouble: return m.dbl;
      case input::Kind::kLong: return m.lng;
    }
  } else {
    const ButtonMap& m = gMap.btn[idx];
    if (e.kind == input::Kind::kClick) return m.click;
    if (e.kind == input::Kind::kLong) return m.lng;
  }
  return Action::kNone;
}

}  // namespace

const char* name(Action a) {
  for (const auto& n : kNames) {
    if (n.a == a) return n.s;
  }
  return "none";
}

Action parse(const char* s) {
  for (const auto& n : kNames) {
    if (strcmp(n.s, s) == 0) return n.a;
  }
  return Action::kNone;
}

Map defaults() {
  Map m;
  m.enc[0] = {Action::kNavPage, Action::kSeek, Action::kNavHome, Action::kNone, Action::kDisplayToggle};
  m.enc[1] = {Action::kVolume, Action::kDisplayBrightness, Action::kPlayPause, Action::kNext, Action::kMute};
  m.btn[0] = {Action::kPrev, Action::kNone};
  m.btn[1] = {Action::kNext, Action::kNone};
  return m;
}

void setMap(const Map& m) { gMap = m; }
const Map& map() { return gMap; }

void handle(const input::Event& e) {
  // While the screen is off, the first input only wakes it.
  if (gScreenOff) {
    gScreenOff = false;
    backlight::set(gBrightness);
    return;
  }
  const Action a = resolve(e);
  if (a == Action::kNone) return;
  const bool rotate = e.kind == input::Kind::kRotate || e.kind == input::Kind::kRotatePressed;
  run(a, rotate ? e.value : 1);
}

void run(Action a, int value) {
  switch (a) {
    case Action::kNone: break;
    case Action::kNavPage: ui::stepPage(value); break;
    case Action::kNavNext: ui::stepPage(1); break;
    case Action::kNavPrev: ui::stepPage(-1); break;
    case Action::kNavHome: ui::showPage(0); break;
    case Action::kPlayPause:
    case Action::kNext:
    case Action::kPrev:
    case Action::kSeek:
    case Action::kVolume:
    case Action::kMute:
      // Host app connected: it executes the action precisely (GSMTC / Core Audio from Phase 6).
      // Otherwise the offline fallback: HID media keys. The demo player on screen follows along.
      if (link::hostConnected()) {
        link::sendAction(name(a), value);
      } else {
        hid::sendMediaAction(a, value);
      }
      ui::mediaAction(a, value);
      break;
    case Action::kDisplayToggle:
      gScreenOff = true;
      backlight::set(0);
      break;
    case Action::kDisplayBrightness: {
      int b = static_cast<int>(gBrightness) + value * 5;
      gBrightness = static_cast<uint8_t>(b < 5 ? 5 : (b > 100 ? 100 : b));
      backlight::set(gBrightness);
      break;
    }
  }
}

}  // namespace actions
}  // namespace rp
