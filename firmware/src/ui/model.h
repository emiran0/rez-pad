// Data the UI shows. Filled by the host link (Phase 5+) and the
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
  uint32_t trackSinceMs = 0;  // device millis() when trackId last changed (UI sets it)
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
  bool valid = false;  // only known while the host is connected
  // The Now Playing app's own volume (Windows volume mixer), when it has an audio session.
  bool appValid = false;
  uint8_t appLevel = 0;
  bool appMuted = false;
  char appName[24] = "";
};

// Album art for track `id` (RGB565, w x h), or `px == nullptr` when that track has none.
// The pixels belong to the media module and stay valid until the next art is taken.
struct Art {
  uint32_t id = 0;
  uint16_t w = 0, h = 0;
  const uint16_t* px = nullptr;
};

constexpr int kMaxStats = 128;  // = cfg::kMaxSources; a SYS_STATS of 128 ids fits one 8 KB packet
struct Stat {
  char id[32] = "";
  float value = 0;
  bool valid = false;
};

// Display units from the config's "units" section (arrive with WEATHER until the config store
// exists, Phase 9). The room sensor is converted with them too.
struct Units {
  bool fahrenheit = false;
  uint8_t wind = 0;  // 0 km/h, 1 m/s, 2 mph
  bool inHg = false;
};

struct Weather {
  bool valid = false;
  char loc[48] = "";
  int64_t updated = 0;  // unix s when the host fetched it (age and 12 h expiry)
  float temp = 0, feels = 0, wind = 0, press = 0;
  bool hasPress = false;
  uint8_t hum = 0;
  uint8_t code = 0;  // WMO weather code
  bool isDay = true;
  struct Hour {
    int64_t t = 0;  // unix s
    float temp = 0;
    uint8_t code = 0;
    uint8_t pop = 0;
    bool isDay = true;
  } hourly[12];
  uint8_t hourCount = 0;
  struct Day {
    int16_t year = 0;  // local date at the location
    uint8_t month = 0, day = 0;
    float min = 0, max = 0;
    uint8_t code = 0;
    uint8_t pop = 0;
  } daily[5];
  uint8_t dayCount = 0;
};

// Audio spectrum from the host (SPECTRUM): levels 0 to 255, log-spaced 50 Hz to 16 kHz.
constexpr int kSpectrumBins = 32;
struct Spectrum {
  uint8_t n = 0;  // bins in use (0 = nothing received yet)
  uint8_t bins[kSpectrumBins] = {};
};

// The Windows volume mixer from the host (MIXER), and the mixer widget's focus.
constexpr int kMixerApps = 8;
struct MixerApp {
  char id[24] = "";
  char name[24] = "";
  uint8_t level = 0;
  bool muted = false;
  bool active = false;
};
struct Mixer {
  uint8_t count = 0;
  MixerApp apps[kMixerApps];
  bool received = false;  // a MIXER arrived since the host connected
  bool focused = false;   // knobs drive the mixer widget on the current page
  int8_t selected = 0;    // position in the focused widget's order (mixer::view)
  const void* focusWidget = nullptr;  // the focused widget's spec (its "apps" order applies)
  char pins[64] = "";     // that widget's "apps" priority list, comma-joined
  bool onlyPins = false;
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
  Art art;
  Stat stats[kMaxStats];
  Weather weather;
  Units units;
  Room room;
  Spectrum spectrum;
  Mixer mixer;
  bool hostConnected = false;
  int clockMinutes = -1;  // minutes since midnight, -1 = unknown
  int64_t unixTime = 0;   // unix s from the synced clock, 0 = unknown (updated every loop)
  int tzOffsetMin = 0;    // PC time zone offset, for local dates
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
  kChangedMixer = 1u << 10,  // mixer apps, focus or selection
  kChangedSpectrum = 1u << 9,  // up to 30 times a second; only the shown page redraws it
  kChangedAll = 0xFFFFFFFFu,
};

Model& model();

// Returns the stat value for a source id, or nullptr when unknown or invalid.
const Stat* findStat(const char* id);

}  // namespace ui
}  // namespace rp
