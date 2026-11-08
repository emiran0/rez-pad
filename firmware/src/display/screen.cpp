#include "screen.h"

#include <Arduino.h>

#include "backlight.h"
#include "link/link.h"
#include "link/usb_device.h"
#include "ui/lvgl_port.h"

namespace rp {
namespace screen {

namespace {

constexpr float kFadePerMs = 100.0f / 300.0f;  // 0 to 100 % in 300 ms

Policy gPolicy;
float gLevel = 0;           // what the backlight shows, fading toward the target
uint8_t gWritten = 255;     // last value sent to the backlight
bool gNeedFrame = false;    // LVGL woke up; draw a full frame before the backlight comes on
uint32_t gLastMs = 0;

int localMin() { return link::localMinutes(); }

}  // namespace

void apply(const cfg::Config& c) {
  Settings s;
  s.brightness = c.brightness;
  s.nightEnabled = c.nightEnabled;
  s.nightFromMin = c.nightFromMin;
  s.nightToMin = c.nightToMin;
  s.nightBrightness = c.nightBrightness;
  s.sleepOnPcSleep = c.sleepOnPcSleep;
  gPolicy.setSettings(s);
}

void tick(uint32_t nowMs) {
  const uint32_t dt = gLastMs == 0 ? 0 : nowMs - gLastMs;
  gLastMs = nowMs;

  const bool suspended = usb::suspended();
  if (suspended != gPolicy.usbSuspended()) {
    gPolicy.setUsbSuspended(suspended);
    Serial.printf("[screen] PC %s\n", suspended ? "asleep" : "awake");
  }
  const float want = gPolicy.target(localMin(), nowMs);

  if (want > 0 && lvgl_port::sleeping()) {
    lvgl_port::setSleeping(false);  // invalidates; ui::loop refreshes the widgets next
    gNeedFrame = true;
    return;
  }
  if (gNeedFrame) {
    gNeedFrame = false;
    lvgl_port::refreshNow();
  }

  if (gLevel != want) {
    const float step = kFadePerMs * static_cast<float>(dt == 0 ? 1 : dt);
    if (gLevel < want) gLevel = gLevel + step > want ? want : gLevel + step;
    else gLevel = gLevel - step < want ? want : gLevel - step;
  }
  const uint8_t out = static_cast<uint8_t>(gLevel + 0.5f);
  if (out != gWritten) {
    gWritten = out;
    backlight::set(out);
  }
  if (want == 0 && gLevel == 0 && !lvgl_port::sleeping()) lvgl_port::setSleeping(true);
}

bool onInput() {
  const bool wakeOnly = gPolicy.onInput(localMin(), millis());
  if (wakeOnly) Serial.println("[screen] woken by input");
  return wakeOnly;
}

void turnOff() { gPolicy.turnOff(); }

void adjust(int detents) { gPolicy.adjust(detents, localMin(), millis()); }

uint8_t target() { return gPolicy.target(localMin(), millis()); }

const Policy& policy() { return gPolicy; }

}  // namespace screen
}  // namespace rp
