// Data the UI shows. Filled with mock data in Phase 3; by the host link (Phase 5+) and the
// sensor task later. Only the UI loop reads or writes it.
#pragma once

#include <stdint.h>

namespace rp {
namespace ui {

enum class PlayState : uint8_t { kNone, kStopped, kPaused, kPlaying };

struct NowPlaying {
  PlayState state = PlayState::kNone;
  uint32_t trackId = 0;
  char title[128] = "";
  char artist[96] = "";
  char album[96] = "";
  char app[32] = "";
  uint32_t posMs = 0;      // position when sampled
  uint32_t durMs = 0;
  uint32_t sampledAtMs = 0;  // device millis() when posMs was valid (for local interpolation)
  bool canSeek = false, canNext = false, canPrev = false;

  // Interpolated position now.
  uint32_t positionAt(uint32_t nowMs) const {
    if (state != PlayState::kPlaying) return posMs;
    const uint32_t p = posMs + (nowMs - sampledAtMs);
    return (durMs && p > durMs) ? durMs : p;
  }
};

struct Volume {
  uint8_t level = 0;  // 0..100
  bool muted = false;
};

constexpr int kMaxStats = 16;
struct Stat {
  char id[32] = "";
  float value = 0;
  bool valid = false;
};

struct Weather {
  bool valid = false;
  char loc[48] = "";
  uint32_t updatedMs = 0;
  float temp = 0, feels = 0, wind = 0;
  uint8_t hum = 0;
  uint8_t code = 0;  // WMO weather code
  bool isDay = true;
  struct Day {
    char name[4] = "";
    float min = 0, max = 0;
    uint8_t code = 0;
    uint8_t pop = 0;
  } daily[5];
};

// Live room climate from the device's own sensor (always real, also in demo mode).
struct Room {
  bool tempValid = false;
  bool pressValid = false;
  float temp = 0, hum = 0, press = 0;
  uint8_t trend = 0;  // sensor::Trend: 0 unknown, 1 falling, 2 steady, 3 rising
  float delta3h = 0;
  uint32_t samples = 0;
};

struct Model {
  NowPlaying np;
  Volume volume;
  Stat stats[kMaxStats];
  Weather weather;
  Room room;
  bool hostConnected = false;
  bool demo = false;  // showing mock data
  int clockMinutes = -1;  // minutes since midnight, -1 = unknown
};

// What changed since the last refresh; widgets only redraw what concerns them.
enum Changed : uint32_t {
  kChangedMedia = 1u << 0,
  kChangedArt = 1u << 1,
  kChangedVolume = 1u << 2,
  kChangedStats = 1u << 3,
  kChangedWeather = 1u << 4,
  kChangedRoom = 1u << 5,
  kChangedClock = 1u << 6,
  kChangedHost = 1u << 7,
  kChangedTick = 1u << 8,  // periodic tick (progress bar interpolation, ages)
  kChangedAll = 0xFFFFFFFFu,
};

Model& model();

// Returns the stat value for a source id, or nullptr when unknown or invalid.
const Stat* findStat(const char* id);

}  // namespace ui
}  // namespace rp
