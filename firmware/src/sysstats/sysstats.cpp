#include "sysstats.h"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <string.h>

namespace rp {
namespace sysstats {

namespace {
portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED;
ui::Stat gStats[ui::kMaxStats];
bool gNew = false;
uint32_t gReceived = 0;
uint32_t gDropped = 0;
}  // namespace

void onSysStats(const uint8_t* body, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, body, len) != DeserializationError::Ok) return;
  JsonObject v = doc["v"];
  if (v.isNull()) return;
  static ui::Stat parsed[ui::kMaxStats];  // 5 KB: static, not on the link task stack (only caller)
  int n = 0;
  for (JsonPair kv : v) {
    const char* id = kv.key().c_str();
    if (n >= ui::kMaxStats || strlen(id) >= sizeof(parsed[0].id)) {
      gDropped++;
      continue;
    }
    ui::Stat& s = parsed[n++];
    strcpy(s.id, id);
    // null = the PC has no value for this source (no driver, no admin): the widget shows "--".
    s.valid = kv.value().is<float>();
    s.value = s.valid ? kv.value().as<float>() : 0.0f;
  }
  taskENTER_CRITICAL(&gLock);
  memcpy(gStats, parsed, sizeof(gStats));
  gNew = true;
  taskEXIT_CRITICAL(&gLock);
  gReceived++;
}

bool take(ui::Stat (&out)[ui::kMaxStats]) {
  taskENTER_CRITICAL(&gLock);
  const bool fresh = gNew;
  if (fresh) {
    memcpy(out, gStats, sizeof(gStats));
    gNew = false;
  }
  taskEXIT_CRITICAL(&gLock);
  return fresh;
}

uint32_t received() { return gReceived; }
uint32_t dropped() { return gDropped; }

}  // namespace sysstats
}  // namespace rp
