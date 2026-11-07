#include "mock.h"

#include <esp_random.h>
#include <stdio.h>
#include <string.h>

namespace rp {
namespace ui {
namespace mock {

namespace {

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

}  // namespace

uint32_t begin(Model& m, uint32_t nowMs) {
  m.demo = true;
  m.hostConnected = false;

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

uint32_t update(Model& m, uint32_t) {
  uint32_t changed = kChangedStats;
  for (int i = 0; i < kStatCount; ++i) {
    const StatDef& d = kStats[i];
    Stat& s = m.stats[i];
    // Random walk pulled back towards the base value.
    float v = s.value + (rnd() - 0.5f) * d.spread * 0.6f + (d.base - s.value) * 0.15f;
    if (v < d.min) v = d.min;
    if (v > d.max) v = d.max;
    s.value = v;
  }
  return changed;
}

}  // namespace mock
}  // namespace ui
}  // namespace rp
