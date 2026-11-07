#include "mock.h"

#include <esp_random.h>
#include <stdio.h>
#include <string.h>

#include "rezpad_protocol.h"

namespace rp {
namespace ui {
namespace mock {

namespace {

struct Track {
  const char* title;
  const char* artist;
  const char* album;
  const char* app;
  uint32_t durMs;
};

// Made-up tracks that exercise the fonts: Turkish, Polish, German, Cyrillic, Greek, a long title
// for the marquee, and CJK (not in the built-in fonts, so it must show placeholder boxes).
const Track kTracks[] = {
    {"Gökyüzü Işıkları", "Çağrı Öztürk", "İstanbul Geceleri", "Spotify", 214000},
    {"Żółta łódź na Wiśle", "Łucja Dąbrowska", "Śląskie opowieści", "Spotify", 187000},
    {"Grüße aus Köln", "Jörg Weiß", "Straßenmusik", "Chrome", 243000},
    {"Ночной город", "Белые ночи", "Северный ветер", "Spotify", 201000},
    {"Ωκεανός", "Νεφέλη", "Θάλασσα", "Spotify", 256000},
    {"A Very Long Track Title That Has To Scroll Across The Whole Card", "Marquee Test Ensemble",
     "Extended Edition (Remastered 2026)", "Firefox", 362000},
    {"夜の散歩", "名前のない歌手", "東京", "Spotify", 228000},
};
constexpr int kTrackCount = sizeof(kTracks) / sizeof(kTracks[0]);
int gTrack = 0;

uint32_t gClockBaseMs = 0;
constexpr int kClockStartMin = 14 * 60 + 32;

struct StatDef {
  const char* id;
  float base, spread, min, max;
};
const StatDef kStats[] = {
    {"sys.cpu.load", 18, 14, 0, 100},   {"sys.cpu.temp", 52, 8, 30, 100},
    {"sys.gpu.load", 35, 25, 0, 100},   {"sys.gpu.temp", 58, 6, 30, 100},
    {"sys.ram.used_pct", 63, 3, 0, 100}, {"sys.net.down_kbps", 900, 800, 0, 100000},
    {"sys.net.up_kbps", 80, 60, 0, 100000},
};
constexpr int kStatCount = sizeof(kStats) / sizeof(kStats[0]);

float rnd() { return (esp_random() & 0xFFFF) / 65535.0f; }  // 0..1

void loadTrack(Model& m, uint32_t nowMs, bool keepState) {
  const Track& t = kTracks[gTrack];
  auto& np = m.np;
  strncpy(np.title, t.title, sizeof(np.title) - 1);
  strncpy(np.artist, t.artist, sizeof(np.artist) - 1);
  strncpy(np.album, t.album, sizeof(np.album) - 1);
  strncpy(np.app, t.app, sizeof(np.app) - 1);
  char key[300];
  snprintf(key, sizeof(key), "%s\n%s\n%s", t.title, t.artist, t.album);
  np.trackId = proto::crc32(reinterpret_cast<const uint8_t*>(key), strlen(key));  // as in docs/protocol.md
  np.durMs = t.durMs;
  np.posMs = 0;
  np.sampledAtMs = nowMs;
  np.canSeek = np.canNext = np.canPrev = true;
  if (!keepState) np.state = PlayState::kPlaying;
}

}  // namespace

uint32_t begin(Model& m, uint32_t nowMs) {
  m.demo = true;
  m.hostConnected = false;
  gClockBaseMs = nowMs;
  m.clockMinutes = kClockStartMin;

  gTrack = 0;
  loadTrack(m, nowMs, false);
  m.np.posMs = 81234;
  m.volume.level = 42;
  m.volume.muted = false;

  for (int i = 0; i < kStatCount && i < kMaxStats; ++i) {
    strncpy(m.stats[i].id, kStats[i].id, sizeof(m.stats[i].id) - 1);
    m.stats[i].value = kStats[i].base;
    m.stats[i].valid = true;
  }

  auto& w = m.weather;
  w.valid = true;
  strncpy(w.loc, "Example City", sizeof(w.loc) - 1);
  w.updatedMs = nowMs;
  w.temp = 14.2f;
  w.feels = 12.9f;
  w.hum = 71;
  w.wind = 12.0f;
  w.code = 3;
  w.isDay = false;
  static const char* kDays[5] = {"Fri", "Sat", "Sun", "Mon", "Tue"};
  static const uint8_t kCodes[5] = {61, 3, 1, 80, 0};
  static const float kMin[5] = {9.1f, 8.4f, 7.0f, 10.2f, 6.5f};
  static const float kMax[5] = {16.4f, 15.0f, 17.8f, 14.1f, 18.3f};
  for (int i = 0; i < 5; ++i) {
    strncpy(w.daily[i].name, kDays[i], sizeof(w.daily[i].name) - 1);
    w.daily[i].code = kCodes[i];
    w.daily[i].min = kMin[i];
    w.daily[i].max = kMax[i];
    w.daily[i].pop = 0;
  }
  return kChangedAll;
}

uint32_t update(Model& m, uint32_t nowMs) {
  uint32_t changed = kChangedStats;
  const int minutes = kClockStartMin + static_cast<int>((nowMs - gClockBaseMs) / 60000);
  if (minutes % 1440 != m.clockMinutes) {
    m.clockMinutes = minutes % 1440;
    changed |= kChangedClock;
  }
  for (int i = 0; i < kStatCount; ++i) {
    const StatDef& d = kStats[i];
    Stat& s = m.stats[i];
    // Random walk pulled back towards the base value.
    float v = s.value + (rnd() - 0.5f) * d.spread * 0.6f + (d.base - s.value) * 0.15f;
    if (v < d.min) v = d.min;
    if (v > d.max) v = d.max;
    s.value = v;
  }
  // Track ended: go to the next one.
  if (m.np.state == PlayState::kPlaying && m.np.positionAt(nowMs) >= m.np.durMs) {
    changed |= nextTrack(m, nowMs, +1);
  }
  return changed;
}

uint32_t playPause(Model& m, uint32_t nowMs) {
  auto& np = m.np;
  np.posMs = np.positionAt(nowMs);
  np.sampledAtMs = nowMs;
  np.state = np.state == PlayState::kPlaying ? PlayState::kPaused : PlayState::kPlaying;
  return kChangedMedia;
}

uint32_t nextTrack(Model& m, uint32_t nowMs, int dir) {
  // "Previous" restarts the track when more than 3 s in, like most players.
  if (dir < 0 && m.np.positionAt(nowMs) > 3000) {
    m.np.posMs = 0;
    m.np.sampledAtMs = nowMs;
    return kChangedMedia;
  }
  gTrack = (gTrack + (dir >= 0 ? 1 : kTrackCount - 1)) % kTrackCount;
  loadTrack(m, nowMs, true);
  if (m.np.state != PlayState::kPaused) m.np.state = PlayState::kPlaying;
  return kChangedMedia | kChangedArt;
}

uint32_t seek(Model& m, uint32_t nowMs, int seconds) {
  auto& np = m.np;
  int64_t p = static_cast<int64_t>(np.positionAt(nowMs)) + static_cast<int64_t>(seconds) * 1000;
  if (p < 0) p = 0;
  if (p > np.durMs) p = np.durMs;
  np.posMs = static_cast<uint32_t>(p);
  np.sampledAtMs = nowMs;
  return kChangedMedia;
}

uint32_t volume(Model& m, int delta) {
  int v = m.volume.level + delta;
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  m.volume.level = static_cast<uint8_t>(v);
  if (delta > 0) m.volume.muted = false;
  return kChangedVolume;
}

uint32_t mute(Model& m) {
  m.volume.muted = !m.volume.muted;
  return kChangedVolume;
}

}  // namespace mock
}  // namespace ui
}  // namespace rp
