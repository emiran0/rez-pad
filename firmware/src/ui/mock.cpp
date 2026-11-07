#include "mock.h"

#include <stdio.h>
#include <string.h>

namespace rp {
namespace ui {
namespace mock {

uint32_t begin(Model& m, uint32_t nowMs) {
  m.demo = true;
  m.hostConnected = false;

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

}  // namespace mock
}  // namespace ui
}  // namespace rp
