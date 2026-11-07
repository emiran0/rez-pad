#include "ui.h"

#include <Arduino.h>

#include "default_layout.h"
#include "link/link.h"
#include "lvgl_port.h"
#include "media/media.h"
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
constexpr uint32_t kMockMs = 1000;   // demo stats
constexpr int kVolumeStep = 2;       // media.volume_step default (the host applies the real one)
constexpr int kSeekStepS = 5;        // media.seek_step_s default (the host applies the real one)
constexpr uint32_t kArtRequestMs = 2500;  // no art this long after a track change: ask for it

uint32_t gPending = 0;
uint32_t gArtRequested = 0;  // track id we already sent ART_REQUEST for
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

uint32_t pollMedia(Model& m, uint32_t nowMs) {
  uint32_t changed = 0;
  NowPlaying np;
  if (media::takeNowPlaying(np)) {
    if (np.trackId != m.np.trackId || (np.state == PlayState::kNone) != (m.np.state == PlayState::kNone)) {
      np.trackSinceMs = nowMs;
      // For docs/measurements.md: host sample time (synced clock) to the widget update; LVGL draws
      // it within the next frame (33 ms).
      Serial.printf("[ui] track %08lX \"%s\" (%s), %ld ms after the host sampled it\n",
                    static_cast<unsigned long>(np.trackId), np.title, np.app,
                    static_cast<long>(nowMs - np.sampledAtMs));
    } else {
      np.trackSinceMs = m.np.trackSinceMs;
    }
    m.np = np;
    changed |= kChangedMedia;
  }
  Volume v;
  if (media::takeVolume(v)) {
    m.volume = v;
    changed |= kChangedVolume;
  }
  Art art;
  if (media::takeArt(art)) {
    m.art = art;
    changed |= kChangedArt;
    if (art.id == m.np.trackId) {
      Serial.printf("[ui] art %08lX %s, %lu ms after the track text\n", static_cast<unsigned long>(art.id),
                    art.px ? "shown" : "none", static_cast<unsigned long>(nowMs - m.np.trackSinceMs));
    }
  }
  // Ask once per track if its art has not shown up (lost transfer, device restarted mid-way).
  if (m.hostConnected && m.np.state != PlayState::kNone && m.np.trackId != m.art.id &&
      m.np.trackId != gArtRequested && !media::artTransferActive() && nowMs - m.np.trackSinceMs > kArtRequestMs) {
    gArtRequested = m.np.trackId;
    char body[40];
    snprintf(body, sizeof(body), "{\"id\":%lu}", static_cast<unsigned long>(m.np.trackId));
    link::send(proto::kArtRequest, body);
    Serial.printf("[ui] no art for %08lX, sent ART_REQUEST\n", static_cast<unsigned long>(m.np.trackId));
  }
  return changed;
}

uint32_t pollLink(uint32_t nowMs) {
  Model& m = model();
  uint32_t changed = 0;
  const bool host = link::hostConnected();
  if (host != m.hostConnected) {
    m.hostConnected = host;
    changed |= kChangedHost;
    if (!host) {
      // Offline: Now Playing says "No host connected", volume is unknown (HID keys only).
      m.np = NowPlaying();
      m.np.trackSinceMs = nowMs;
      m.volume = Volume();
      m.art = Art();
      media::releaseArt();
      gArtRequested = 0;
      changed |= kChangedMedia | kChangedVolume | kChangedArt;
    }
  }
  changed |= pollMedia(m, nowMs);
  const int minutes = link::localMinutes();
  if (minutes != m.clockMinutes) {
    m.clockMinutes = minutes;
    changed |= kChangedClock;
  }
  return changed;
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
  gPending |= pollLink(nowMs);

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
  // Optimistic feedback for the knobs while the host is connected: the host confirms with VOLUME
  // or NOW_PLAYING within a few ms, but the number on screen moves with the knob right away.
  // Offline the real volume and position are unknown (HID keys only), so nothing changes here.
  Model& m = model();
  if (!m.hostConnected) return;
  const uint32_t now = millis();
  if (a == actions::Action::kVolume && m.volume.valid) {
    int v = m.volume.level + value * kVolumeStep;
    m.volume.level = static_cast<uint8_t>(v < 0 ? 0 : (v > 100 ? 100 : v));
    if (value > 0) m.volume.muted = false;
    gPending |= kChangedVolume;
  } else if (a == actions::Action::kSeek && m.np.canSeek) {
    int64_t p = static_cast<int64_t>(m.np.positionAt(now)) + static_cast<int64_t>(value) * kSeekStepS * 1000;
    if (p < 0) p = 0;
    if (p > m.np.durMs) p = m.np.durMs;
    m.np.posMs = static_cast<uint32_t>(p);
    m.np.sampledAtMs = now;
    gPending |= kChangedMedia;
  }
}

}  // namespace ui
}  // namespace rp
