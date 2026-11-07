#include "ui.h"

#include <Arduino.h>

#include "default_layout.h"
#include "lvgl_port.h"
#include "mock.h"
#include "model.h"
#include "pages.h"
#include "sensors/sensors.h"
#include "status_bar.h"
#include "theme.h"

namespace rp {
namespace ui {

namespace {

constexpr uint32_t kTickMs = 250;    // progress bar interpolation
constexpr uint32_t kMockMs = 1000;   // demo stats and clock
constexpr int kVolumeStep = 2;       // media.volume_step default
constexpr int kSeekStepS = 5;        // seconds per detent for media.seek

uint32_t gPending = 0;
uint32_t gLastTick = 0;
uint32_t gLastMock = 0;
uint32_t gRoomSamples = 0;

void updatePageIndicator() { status_bar::setPage(pages::current(), pages::count(), pages::name(pages::current())); }

uint32_t pollRoom() {
  const sensors::Reading r = sensors::latest();
  if (r.samples == gRoomSamples) return 0;
  gRoomSamples = r.samples;
  Room& room = model().room;
  room.samples = r.samples;
  room.tempValid = r.ahtOk;
  room.pressValid = r.bmpOk;
  room.temp = r.tempC;
  room.hum = r.humPct;
  room.press = r.pressHpa;
  room.trend = static_cast<uint8_t>(r.trend);
  room.delta3h = r.trendDelta3h;
  return kChangedRoom;
}

}  // namespace

bool begin() {
  if (!lvgl_port::begin()) return false;
  lv_obj_t* scr = lv_screen_active();

  lv_obj_t* grid = lv_obj_create(scr);
  lv_obj_remove_style_all(grid);
  lv_obj_set_pos(grid, 0, kGridY);
  lv_obj_set_size(grid, kScreenW, kGridH);
  lv_obj_set_scrollable(grid, false);

  int count = 0;
  const PageSpec* specs = defaultPages(count);
  pages::build(grid, specs, count, 4, 3);
  status_bar::build(scr);
  updatePageIndicator();

  const uint32_t now = millis();
  gPending |= mock::begin(model(), now);
  gPending |= pollRoom();
  gLastTick = gLastMock = now;
  return true;
}

void loop(uint32_t nowMs) {
  if (nowMs - gLastTick >= kTickMs) {
    gLastTick = nowMs;
    gPending |= kChangedTick;
  }
  if (nowMs - gLastMock >= kMockMs) {
    gLastMock = nowMs;
    gPending |= mock::update(model(), nowMs);
  }
  gPending |= pollRoom();

  if (gPending && !lvgl_port::paused()) {
    const uint32_t changed = gPending;
    gPending = 0;
    status_bar::refresh(model(), changed);
    pages::refresh(model(), changed, nowMs);
  }
  lvgl_port::handle();
}

void showPage(int index) {
  pages::show(index);
  updatePageIndicator();
  gPending |= kChangedTick;  // bring the new page's live parts up to date immediately
}

void stepPage(int delta) {
  pages::step(delta);
  updatePageIndicator();
  gPending |= kChangedTick;
}

void mediaAction(actions::Action a, int value) {
  Model& m = model();
  const uint32_t now = millis();
  switch (a) {
    case actions::Action::kPlayPause: gPending |= mock::playPause(m, now); break;
    case actions::Action::kNext: gPending |= mock::nextTrack(m, now, +1); break;
    case actions::Action::kPrev: gPending |= mock::nextTrack(m, now, -1); break;
    case actions::Action::kSeek: gPending |= mock::seek(m, now, value * kSeekStepS); break;
    case actions::Action::kVolume: gPending |= mock::volume(m, value * kVolumeStep); break;
    case actions::Action::kMute: gPending |= mock::mute(m); break;
    default: break;
  }
}

}  // namespace ui
}  // namespace rp
