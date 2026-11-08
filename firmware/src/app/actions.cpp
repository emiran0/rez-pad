#include "actions.h"

#include <Arduino.h>
#include <string.h>

#include "config/store.h"
#include "display/screen.h"
#include "hid/media_keys.h"
#include "link/link.h"
#include "ui/pages.h"
#include "ui/ui.h"

namespace rp {
namespace actions {

namespace {

Binding actionBinding(Action a) {
  Binding b;
  b.action = a;
  return b;
}

Binding resolve(const input::Event& e) {
  const cfg::Config& c = config::active();
  const int page = ui::pages::current();
  const Map& map = (page >= 0 && page < c.pageCount) ? c.pages[page].inputs : c.inputs;
  if (e.source == input::Source::kEnc1 || e.source == input::Source::kEnc2) {
    const EncoderMap& m = map.enc[e.source == input::Source::kEnc1 ? 0 : 1];
    switch (e.kind) {
      case input::Kind::kRotate: return actionBinding(m.rotate);
      case input::Kind::kRotatePressed: return actionBinding(m.rotatePressed);
      case input::Kind::kClick: return m.click;
      case input::Kind::kDouble: return m.dbl;
      case input::Kind::kLong: return m.lng;
    }
    return Binding();
  }
  const int idx = static_cast<int>(e.source) - static_cast<int>(input::Source::kBtn1);
  if (idx < 0 || idx >= kMaxButtons) return Binding();
  const ButtonMap& m = map.btn[idx];
  switch (e.kind) {
    case input::Kind::kClick: return m.click;
    case input::Kind::kDouble: return m.dbl;
    case input::Kind::kLong: return m.lng;
    default: return Binding();
  }
}

void runBinding(const Binding& b, int value) {
  switch (b.kind) {
    case Binding::Kind::kAction:
      run(b.action, value);
      break;
    case Binding::Kind::kKeys:
      // Sent by the device itself: works with or without the host app.
      hid::sendKeys(b.mods, b.key);
      break;
    case Binding::Kind::kMacro: {
      const cfg::Config& c = config::active();
      const char* id = b.macro >= 0 && b.macro < c.macroCount ? c.macroIds[b.macro] : "";
      if (link::hostConnected()) link::sendMacro(id);
      else Serial.printf("[input] macro %s needs the host app\n", id);
      break;
    }
  }
}

}  // namespace

void handle(const input::Event& e) {
  // While the screen is dark, the first input only wakes it.
  if (screen::onInput()) return;
  // A focused mixer widget takes the knobs' turns and clicks.
  if (ui::mixerInput(e)) return;
  const Binding b = resolve(e);
  if (b.none()) return;
  const bool rotate = e.kind == input::Kind::kRotate || e.kind == input::Kind::kRotatePressed;
  runBinding(b, rotate ? e.value : 1);
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
      // Otherwise the offline fallback: HID media keys.
      if (link::hostConnected()) {
        link::sendAction(name(a), value);
      } else {
        hid::sendMediaAction(a, value);
      }
      ui::mediaAction(a, value);
      break;
    case Action::kAppVolume:
    case Action::kAppMute:
      // Only the host can change one app's volume; there is no media key for it.
      if (link::hostConnected()) {
        link::sendAction(name(a), value);
        ui::mediaAction(a, value);
      } else {
        Serial.printf("[input] %s needs the host app\n", name(a));
      }
      break;
    case Action::kMixerFocus:
      ui::toggleMixerFocus();
      break;
    case Action::kDisplayToggle:
      screen::turnOff();
      break;
    case Action::kDisplayBrightness:
      screen::adjust(value);
      break;
  }
}

}  // namespace actions
}  // namespace rp
