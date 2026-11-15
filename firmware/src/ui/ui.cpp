#include "ui.h"

#include <Arduino.h>

#include "app/settings.h"
#include "assets/assets.h"
#include "config/store.h"
#include <string.h>
#include "link/link.h"
#include "lvgl_port.h"
#include "media/media.h"
#include "audio/mixer.h"
#include "audio/spectrum.h"
#include "sysstats/sysstats.h"
#include "weather/weather.h"
#include "model.h"
#include "pages.h"
#include "popup.h"
#include "display/screen.h"
#include "sensors/sensors.h"
#include "status_bar.h"
#include "theme.h"
#include "widgets/image_widget.h"

namespace rp {
namespace ui {

namespace {

constexpr uint32_t kTickMs = 250;    // progress bar interpolation
constexpr uint32_t kArtRequestMs = 2500;  // no art this long after a track change: ask for it
constexpr uint32_t kSensorSendMs = 10000;  // SENSOR to the host (docs/protocol.md)

lv_obj_t* gGrid = nullptr;
uint32_t gPending = 0;
uint32_t gAssetGen = 0;  // assets::generation() the pages were built with
uint32_t gArtRequested = 0;  // track id we already sent ART_REQUEST for
uint32_t gLastSensorSend = 0;
uint32_t gLastTick = 0;
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
      for (Stat& s : m.stats) s = Stat();  // system widgets show "--"
      m.mixer = Mixer();  // also drops the focus
      changed |= kChangedMixer;
      changed |= kChangedStats;
      gArtRequested = 0;
      changed |= kChangedMedia | kChangedVolume | kChangedArt;
    }
  }
  changed |= pollMedia(m, nowMs);
  if (sysstats::take(m.stats)) changed |= kChangedStats;
  if (weather::take(m.weather, m.units)) changed |= kChangedWeather;
  if (spectrum::update(m.spectrum, nowMs)) changed |= kChangedSpectrum;
  if (mixer::update(m.mixer, nowMs)) changed |= kChangedMixer;
  m.unixTime = link::unixNow();
  m.tzOffsetMin = link::tzOffsetMin();
  const int minutes = link::localMinutes();
  if (minutes != m.clockMinutes) {
    m.clockMinutes = minutes;
    changed |= kChangedClock;
  }
  return changed;
}

// SENSOR: room readings for the host's web page, metric, null when missing.
void sendSensor() {
  const Room& r = model().room;
  if (r.samples == 0) return;
  char body[160];
  char t[16] = "null", h[16] = "null", p[16] = "null", d[16] = "null";
  if (r.tempValid) {
    snprintf(t, sizeof(t), "%.2f", r.temp);
    snprintf(h, sizeof(h), "%.1f", r.hum);
  }
  if (r.pressValid) snprintf(p, sizeof(p), "%.2f", r.press);
  if (r.trend != 0) snprintf(d, sizeof(d), "%.2f", r.delta3h);
  snprintf(body, sizeof(body), "{\"temp_c\":%s,\"hum_pct\":%s,\"press_hpa\":%s,\"trend\":\"%s\",\"delta_3h\":%s}", t, h,
           p, sensor::trendName(static_cast<sensor::Trend>(r.trend)), d);
  link::send(proto::kSensor, body);
}

void setUnits(const cfg::Config& c) {
  Units& u = model().units;
  u.fahrenheit = c.fahrenheit;
  u.wind = c.wind;
  u.inHg = c.inHg;
}

// Builds the pages of the active config (the grid must be empty).
void buildPages() {
  const cfg::Config& c = config::active();
  widgets::ImageWidget::resetMissing();
  gAssetGen = assets::generation();
  pages::build(gGrid, c);
  status_bar::setItems(c);
  status_bar::setWarning(config::warning());
}

// A new config from the host (validated and saved by the link task): rebuild everything, apply
// the settings, then confirm with CONFIG_ACK (docs/protocol.md).
void applyConfig(cfg::Config* next) {
  // The widgets are rebuilt: a focused mixer would point at the old one.
  mixer::unfocus(model().mixer);
  popup::hide();
  const uint32_t t0 = millis();
  const int page = pages::current();
  pages::clear();  // widgets reference the old config's specs: delete them before releasing it
  config::setActive(next);
  buildPages();
  settings::apply(*next);
  setUnits(*next);
  pages::show(page < pages::count() ? page : 0);
  updatePageIndicator();
  gPending |= kChangedAll;
  Serial.printf("[ui] config applied: %u pages, rebuilt in %lu ms\n", next->pageCount,
                static_cast<unsigned long>(millis() - t0));
  assets::collectGarbage(*next);
  // `art`: album art edge the new layout wants (0 = no album_art), like HELLO_ACK.
  char body[96];
  snprintf(body, sizeof(body), "{\"ok\":true,\"crc32\":%lu,\"error\":\"\",\"art\":%d}",
           static_cast<unsigned long>(config::storedCrc()), media::artSize());
  link::send(proto::kConfigAck, body);
}

// Same config, fresh pages (an image file a widget was missing has arrived).
void rebuildPages() {
  const int page = pages::current();
  pages::clear();
  buildPages();
  pages::show(page < pages::count() ? page : 0);
  updatePageIndicator();
  gPending |= kChangedAll;
  Serial.println("[ui] pages rebuilt for new image files");
}

}  // namespace

bool begin() {
  if (!lvgl_port::begin()) return false;
  lv_obj_t* scr = lv_screen_active();

  gGrid = lv_obj_create(scr);
  lv_obj_remove_style_all(gGrid);
  lv_obj_set_pos(gGrid, 0, kGridY);
  lv_obj_set_size(gGrid, kScreenW, kGridH);
  lv_obj_set_scrollable(gGrid, false);

  status_bar::build(scr);
  buildPages();
  popup::build();
  setUnits(config::active());
  updatePageIndicator();

  const uint32_t now = millis();
  gPending |= pollRoom();
  gLastTick = now;
  return true;
}

bool pageHasMixer();

// ---- mixer hints in the status bar ----

uint32_t gMixerHintUntil = 0;  // "how to focus" after a page with a mixer is shown
char gHintShown[96] = "";

// "Hold knob 2" for the gesture mapped to mixer.focus on the current page, or nullptr.
bool focusGesture(char* out, size_t n) {
  const cfg::Config& c = config::active();
  const int p = pages::current();
  const actions::Map& map = (p >= 0 && p < c.pageCount) ? c.pages[p].inputs : c.inputs;
  auto is = [](const actions::Binding& b) {
    return b.kind == actions::Binding::Kind::kAction && b.action == actions::Action::kMixerFocus;
  };
  for (int e = 0; e < actions::kMaxKnobs; ++e) {
    if (!c.knobPorts[e]) continue;
    const actions::EncoderMap& m = map.enc[e];
    if (is(m.click)) return snprintf(out, n, "Click knob %d", e + 1) > 0;
    if (is(m.lng)) return snprintf(out, n, "Hold knob %d", e + 1) > 0;
    if (is(m.dbl)) return snprintf(out, n, "Double-click knob %d", e + 1) > 0;
  }
  for (int b = 0; b < actions::kMaxButtons; ++b) {
    if (!c.buttonPorts[b]) continue;
    const actions::ButtonMap& m = map.btn[b];
    if (is(m.click)) return snprintf(out, n, "Press button %d", b + 1) > 0;
    if (is(m.lng)) return snprintf(out, n, "Hold button %d", b + 1) > 0;
    if (is(m.dbl)) return snprintf(out, n, "Double-press button %d", b + 1) > 0;
  }
  return false;
}

void updateHint(uint32_t nowMs) {
  char hint[96] = "";
  const Model& m = model();
  if (m.mixer.focused) {
    snprintf(hint, sizeof(hint), "Knob 1: app \xC2\xB7 Knob 2: volume, click mutes \xC2\xB7 Click knob 1: exit");
  } else if (static_cast<int32_t>(gMixerHintUntil - nowMs) > 0) {
    char g[32];
    if (focusGesture(g, sizeof(g))) snprintf(hint, sizeof(hint), "%s to control the mixer", g);
    else snprintf(hint, sizeof(hint), "Mixer: map \"Focus the volume mixer\" in the editor");
  }
  if (strcmp(hint, gHintShown) == 0) return;
  strcpy(gHintShown, hint);
  status_bar::setHint(hint);
}

void showMixerHint() {
  gMixerHintUntil = pageHasMixer() ? millis() + 5000 : 0;
}

void loop(uint32_t nowMs) {
  if (nowMs - gLastTick >= kTickMs) {
    gLastTick = nowMs;
    gPending |= kChangedTick;
  }
  if (cfg::Config* next = config::take()) applyConfig(next);
  updateHint(nowMs);
  // An image widget was built before its file arrived: rebuild once new files are stored.
  if (widgets::ImageWidget::missing() > 0 && assets::generation() != gAssetGen) rebuildPages();
  gPending |= pollRoom();
  gPending |= pollLink(nowMs);
  if (model().hostConnected && nowMs - gLastSensorSend >= kSensorSendMs) {
    gLastSensorSend = nowMs;
    sendSensor();
  }

  uint32_t changed = 0;
  if (gPending && !lvgl_port::paused()) {
    changed = gPending;
    gPending = 0;
    status_bar::refresh(model(), changed);
    pages::refresh(model(), changed, nowMs);
  }
  // A dark screen (or the debug view) drops the popup; it would be stale when the screen wakes.
  if (lvgl_port::paused()) popup::hide();
  else popup::refresh(model(), changed, nowMs);
  lvgl_port::handle();
}

const cfg::WidgetSpec* pageMixer() {
  const cfg::Config& c = config::active();
  const int p = pages::current();
  if (p < 0 || p >= c.pageCount) return nullptr;
  for (int i = 0; i < c.pages[p].widgetCount; ++i) {
    if (strcmp(c.pages[p].widgets[i].type, "mixer") == 0) return &c.pages[p].widgets[i];
  }
  return nullptr;
}

bool pageHasMixer() {
  const cfg::Config& c = config::active();
  const int p = pages::current();
  if (p < 0 || p >= c.pageCount) return false;
  for (int i = 0; i < c.pages[p].widgetCount; ++i) {
    if (strcmp(c.pages[p].widgets[i].type, "mixer") == 0) return true;
  }
  return false;
}

void unfocusMixer() {
  if (!model().mixer.focused) return;
  mixer::unfocus(model().mixer);
  gPending |= kChangedMixer;
}

void toggleMixerFocus() {
  popup::hide();  // the knobs now drive the mixer, which shows its own levels
  Model& m = model();
  if (mixer::toggleFocus(m.mixer, pageMixer(), m.hostConnected, millis())) gPending |= kChangedMixer;
}

bool mixerInput(const input::Event& e) {
  Model& m = model();
  if (!mixer::handleInput(m.mixer, e, config::active().volumeStep, millis())) return false;
  gPending |= kChangedMixer;
  return true;
}

// widgets::Shows bit a popup kind stands for (0: no widget shows it, e.g. brightness).
uint32_t popupShows(popup::Kind k) {
  switch (k) {
    case popup::Kind::kVolume: return widgets::kShowsVolume;
    case popup::Kind::kAppVolume: return widgets::kShowsAppVolume;
    case popup::Kind::kSeek: return widgets::kShowsSeek;
    default: return 0;
  }
}

// After a page switch the popup stays for the rest of its time, unless the new page shows the
// same value in a widget.
void popupAfterPageChange() {
  const uint32_t need = popupShows(popup::shown());
  if (need && (pages::currentShows() & need)) popup::hide();
}

void showPage(int index) {
  unfocusMixer();
  pages::show(index);
  popupAfterPageChange();
  updatePageIndicator();
  showMixerHint();
  gPending |= kChangedTick;  // bring the new page's live parts up to date immediately
}

void stepPage(int delta) {
  unfocusMixer();
  pages::step(delta);
  popupAfterPageChange();
  updatePageIndicator();
  showMixerHint();
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
    int v = m.volume.level + value * config::active().volumeStep;
    m.volume.level = static_cast<uint8_t>(v < 0 ? 0 : (v > 100 ? 100 : v));
    if (value > 0) m.volume.muted = false;
    gPending |= kChangedVolume;
  } else if (a == actions::Action::kAppVolume && m.volume.appValid) {
    int v = m.volume.appLevel + value * config::active().volumeStep;
    m.volume.appLevel = static_cast<uint8_t>(v < 0 ? 0 : (v > 100 ? 100 : v));
    if (value > 0) m.volume.appMuted = false;
    gPending |= kChangedVolume;
  } else if (a == actions::Action::kSeek && m.np.canSeek) {
    int64_t p = static_cast<int64_t>(m.np.positionAt(now)) +
                static_cast<int64_t>(value) * config::active().seekStepS * 1000;
    if (p < 0) p = 0;
    if (p > m.np.durMs) p = m.np.durMs;
    m.np.posMs = static_cast<uint32_t>(p);
    m.np.sampledAtMs = now;
    gPending |= kChangedMedia;
  }
}

void actionFeedback(actions::Action a) {
  using actions::Action;
  const Model& m = model();
  popup::Kind kind = popup::Kind::kNone;
  switch (a) {
    case Action::kVolume:
    case Action::kMute:
      kind = popup::Kind::kVolume;  // offline too: the keys still go out, the level is unknown
      break;
    case Action::kAppVolume:
    case Action::kAppMute:
      if (m.hostConnected) kind = popup::Kind::kAppVolume;  // offline these do nothing
      break;
    case Action::kSeek:
      if (m.hostConnected) kind = popup::Kind::kSeek;  // offline seek is ignored
      break;
    case Action::kDisplayBrightness:
      kind = popup::Kind::kBrightness;
      break;
    default:
      return;  // page changes and the rest: nothing to show
  }
  if (kind == popup::Kind::kNone) return;
  const uint32_t need = popupShows(kind);
  if (need && (pages::currentShows() & need)) return;  // a widget on this page shows it already
  popup::show(kind, m, millis(), screen::target());
}

}  // namespace ui
}  // namespace rp
