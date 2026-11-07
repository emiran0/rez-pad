#include "weather.h"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <stdio.h>
#include <string.h>

namespace rp {
namespace weather {

namespace {
portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED;
ui::Weather gWeather;
ui::Units gUnits;
bool gNew = false;
uint32_t gReceived = 0;

// Copies UTF-8 text, cutting at a character boundary when it does not fit.
void copyUtf8(char* dst, size_t cap, const char* src) {
  size_t n = strlen(src);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && (static_cast<uint8_t>(src[n]) & 0xC0) == 0x80) --n;
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

uint8_t u8(JsonVariantConst v) {
  const int i = v | 0;
  return static_cast<uint8_t>(i < 0 ? 0 : (i > 255 ? 255 : i));
}
}  // namespace

void onWeather(const uint8_t* body, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, body, len) != DeserializationError::Ok) return;

  // Large struct: built in static storage, the link task's stack stays small.
  static ui::Weather w;
  w = ui::Weather();
  ui::Units units;
  JsonObjectConst u = doc["units"];
  units.fahrenheit = strcmp(u["temp"] | "c", "f") == 0;
  const char* wind = u["wind"] | "kmh";
  units.wind = strcmp(wind, "ms") == 0 ? 1 : (strcmp(wind, "mph") == 0 ? 2 : 0);
  units.inHg = strcmp(u["pressure"] | "hpa", "inhg") == 0;

  copyUtf8(w.loc, sizeof(w.loc), doc["loc"] | "");
  w.updated = doc["updated"] | static_cast<int64_t>(0);
  JsonObjectConst now = doc["now"];
  w.temp = now["temp"] | 0.0f;
  w.feels = now["feels"] | 0.0f;
  w.wind = now["wind"] | 0.0f;
  w.hasPress = now["press"].is<float>();
  w.press = now["press"] | 0.0f;
  w.hum = u8(now["hum"]);
  w.code = u8(now["code"]);
  w.isDay = now["is_day"] | true;

  for (JsonObjectConst h : doc["hourly"].as<JsonArrayConst>()) {
    if (w.hourCount >= sizeof(w.hourly) / sizeof(w.hourly[0])) break;
    auto& o = w.hourly[w.hourCount++];
    o.t = h["t"] | static_cast<int64_t>(0);
    o.temp = h["temp"] | 0.0f;
    o.code = u8(h["code"]);
    o.pop = u8(h["pop"]);
    o.isDay = h["is_day"] | true;
  }
  for (JsonObjectConst d : doc["daily"].as<JsonArrayConst>()) {
    if (w.dayCount >= sizeof(w.daily) / sizeof(w.daily[0])) break;
    auto& o = w.daily[w.dayCount];
    int y = 0, m = 0, day = 0;
    if (sscanf(d["d"] | "", "%d-%d-%d", &y, &m, &day) != 3) continue;
    o.year = static_cast<int16_t>(y);
    o.month = static_cast<uint8_t>(m);
    o.day = static_cast<uint8_t>(day);
    o.min = d["min"] | 0.0f;
    o.max = d["max"] | 0.0f;
    o.code = u8(d["code"]);
    o.pop = u8(d["pop"]);
    w.dayCount++;
  }
  w.valid = w.updated > 0;

  taskENTER_CRITICAL(&gLock);
  gWeather = w;
  gUnits = units;
  gNew = true;
  taskEXIT_CRITICAL(&gLock);
  gReceived++;
}

bool take(ui::Weather& w, ui::Units& units) {
  // About 500 bytes; copying inside the critical section takes a few microseconds.
  taskENTER_CRITICAL(&gLock);
  const bool fresh = gNew;
  if (fresh) {
    w = gWeather;
    units = gUnits;
    gNew = false;
  }
  taskEXIT_CRITICAL(&gLock);
  return fresh;
}

uint32_t received() { return gReceived; }

}  // namespace weather
}  // namespace rp
