#include "mixer.h"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <string.h>

#include "link/link.h"

namespace rp {
namespace mixer {

namespace {

constexpr uint32_t kFocusTimeoutMs = 15000;

portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED;
ui::MixerApp gApps[ui::kMixerApps];
uint8_t gCount = 0;
bool gNew = false;
uint32_t gLastInputMs = 0;

}  // namespace

void onMixer(const uint8_t* body, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, body, len) != DeserializationError::Ok) return;
  JsonArray apps = doc["apps"];
  if (apps.isNull()) return;
  ui::MixerApp parsed[ui::kMixerApps];
  uint8_t n = 0;
  for (JsonObject a : apps) {
    if (n >= ui::kMixerApps) break;
    ui::MixerApp& p = parsed[n++];
    strlcpy(p.id, a["id"] | "", sizeof(p.id));
    strlcpy(p.name, a["name"] | "", sizeof(p.name));
    const int level = a["level"] | 0;
    p.level = static_cast<uint8_t>(level < 0 ? 0 : (level > 100 ? 100 : level));
    p.muted = a["muted"] | false;
    p.active = a["active"] | false;
  }
  taskENTER_CRITICAL(&gLock);
  memcpy(gApps, parsed, sizeof(gApps));
  gCount = n;
  gNew = true;
  taskEXIT_CRITICAL(&gLock);
}

int view(const ui::Mixer& m, const char* pins, bool only, uint8_t (&out)[ui::kMixerApps]) {
  int n = 0;
  bool used[ui::kMixerApps] = {};
  // Pinned ids first, in the list's order.
  const char* p = pins ? pins : "";
  while (*p && n < ui::kMixerApps) {
    const char* end = strchr(p, ',');
    const size_t len = end ? static_cast<size_t>(end - p) : strlen(p);
    for (int i = 0; i < m.count; ++i) {
      if (!used[i] && strlen(m.apps[i].id) == len && strncmp(m.apps[i].id, p, len) == 0) {
        used[i] = true;
        out[n++] = static_cast<uint8_t>(i);
        break;
      }
    }
    p = end ? end + 1 : p + len;
  }
  if (only) return n;
  for (int i = 0; i < m.count && n < ui::kMixerApps; ++i) {
    if (!used[i]) out[n++] = static_cast<uint8_t>(i);
  }
  return n;
}

namespace {

// The app selected in the focused widget's order, or nullptr.
ui::MixerApp* selectedApp(ui::Mixer& m) {
  uint8_t order[ui::kMixerApps];
  const int n = view(m, m.pins, m.onlyPins, order);
  return m.selected >= 0 && m.selected < n ? &m.apps[order[m.selected]] : nullptr;
}

}  // namespace

bool update(ui::Mixer& m, uint32_t nowMs) {
  bool changed = false;
  ui::MixerApp apps[ui::kMixerApps];
  uint8_t n = 0;
  taskENTER_CRITICAL(&gLock);
  const bool fresh = gNew;
  if (fresh) {
    memcpy(apps, gApps, sizeof(apps));
    n = gCount;
    gNew = false;
  }
  taskEXIT_CRITICAL(&gLock);
  if (fresh) {
    // Keep the same app selected when the list changes around it.
    char selectedId[sizeof(m.apps[0].id)] = "";
    if (const ui::MixerApp* a = selectedApp(m)) strcpy(selectedId, a->id);
    memcpy(m.apps, apps, sizeof(apps));
    m.count = n;
    m.received = true;
    uint8_t order[ui::kMixerApps];
    const int shown = view(m, m.pins, m.onlyPins, order);
    int sel = 0;
    for (int i = 0; i < shown; ++i) {
      if (strcmp(m.apps[order[i]].id, selectedId) == 0) sel = i;
    }
    m.selected = static_cast<int8_t>(sel);
    if (shown == 0) m.focused = false;
    changed = true;
  }
  if (m.focused && nowMs - gLastInputMs > kFocusTimeoutMs) {
    m.focused = false;
    changed = true;
  }
  return changed;
}

bool toggleFocus(ui::Mixer& m, const cfg::WidgetSpec* widget, bool hostConnected, uint32_t nowMs) {
  if (m.focused) {
    m.focused = false;
    return true;
  }
  if (!widget || !hostConnected) return false;
  strlcpy(m.pins, widget->apps, sizeof(m.pins));
  m.onlyPins = widget->onlyApps;
  uint8_t order[ui::kMixerApps];
  const int shown = view(m, m.pins, m.onlyPins, order);
  if (shown == 0) return false;
  m.focused = true;
  m.focusWidget = widget;
  m.selected = 0;  // start at the top of this widget's order (its first priority app)
  gLastInputMs = nowMs;
  return true;
}

void unfocus(ui::Mixer& m) { m.focused = false; }

bool handleInput(ui::Mixer& m, const input::Event& e, int volumeStep, uint32_t nowMs) {
  if (!m.focused) return false;
  const bool enc1 = e.source == input::Source::kEnc1;
  const bool enc2 = e.source == input::Source::kEnc2;
  if (!enc1 && !enc2) return false;
  gLastInputMs = nowMs;
  uint8_t order[ui::kMixerApps];
  const int shown = view(m, m.pins, m.onlyPins, order);
  if (e.kind == input::Kind::kRotate) {
    if (shown == 0) return true;
    if (enc1) {
      int s = m.selected + e.value;
      if (s < 0) s = 0;
      if (s >= shown) s = shown - 1;
      m.selected = static_cast<int8_t>(s);
      return true;
    }
    if (m.selected < 0 || m.selected >= shown) return true;
    ui::MixerApp& a = m.apps[order[m.selected]];
    link::sendActionId("mixer.volume", e.value, a.id);
    // Show it right away; the host confirms with the next MIXER.
    int v = a.level + e.value * volumeStep;
    a.level = static_cast<uint8_t>(v < 0 ? 0 : (v > 100 ? 100 : v));
    if (e.value > 0) a.muted = false;
    return true;
  }
  if (e.kind == input::Kind::kClick) {
    if (enc1) {
      m.focused = false;
      return true;
    }
    if (m.selected >= 0 && m.selected < shown) {
      ui::MixerApp& a = m.apps[order[m.selected]];
      link::sendActionId("mixer.mute", 1, a.id);
      a.muted = !a.muted;
    }
    return true;
  }
  return false;  // double, long, press-and-turn: their normal mapping
}

}  // namespace mixer
}  // namespace rp
