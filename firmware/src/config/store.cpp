#include "store.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <new>
#include <string.h>

#include "default_config.h"
#include "rezpad_protocol.h"

namespace rp {
namespace config {

namespace {

constexpr const char* kPath = "/config.json";
constexpr const char* kTmpPath = "/config.tmp";

cfg::Config* gActive = nullptr;
bool gActiveStatic = false;  // boot fallback when PSRAM allocation failed, never released
char gWarning[160] = "";
volatile uint32_t gStoredCrc = 0;
bool gMounted = false;

portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED;
cfg::Config* gPosted = nullptr;

uint8_t* psramBuffer(size_t n) {
  return static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

// Reads a whole file into a new PSRAM buffer (caller frees). 0 when missing or unreadable.
size_t readFile(const char* path, uint8_t** out) {
  *out = nullptr;
  File f = LittleFS.open(path, "r");
  if (!f) return 0;
  const size_t n = f.size();
  if (n == 0 || n > cfg::kMaxConfigBytes) {
    f.close();
    return 0;
  }
  uint8_t* buf = psramBuffer(n);
  if (!buf) {
    f.close();
    return 0;
  }
  const size_t got = f.read(buf, n);
  f.close();
  if (got != n) {
    heap_caps_free(buf);
    return 0;
  }
  *out = buf;
  return n;
}

// The screen the device was last set up for (display.panel and display.orientation of the last
// valid config), kept in NVS so a fallback to the built-in default still fits the panel.
uint8_t gProfilePanel = 0;
uint8_t gProfileOrientation = 0;

void loadProfile() {
  Preferences p;
  if (!p.begin("display", true)) return;  // nothing stored yet: ILI9488 landscape
  gProfilePanel = p.getUChar("panel", 0);
  gProfileOrientation = p.getUChar("orient", 0);
  p.end();
  if (gProfilePanel >= cfg::kPanelCount) gProfilePanel = 0;
  if (gProfileOrientation >= cfg::kOrientationCount) gProfileOrientation = 0;
}

void useDefault(cfg::Config* c) {
  char err[128];
  if (!cfg::parseDefault(gProfilePanel, gProfileOrientation, *c, err, sizeof(err))) {
    // Cannot happen (the native tests parse every layout); fall back to 480x320.
    Serial.printf("[config] built-in default for %s %s invalid: %s\n", cfg::panelName(gProfilePanel),
                  cfg::orientationName(gProfileOrientation), err);
    if (!cfg::parseDefault(0, 0, *c, err, sizeof(err))) Serial.printf("[config] built-in default invalid: %s\n", err);
  }
}

}  // namespace

void rememberProfile(uint8_t panel, uint8_t orientation) {
  if (panel == gProfilePanel && orientation == gProfileOrientation) return;
  Preferences p;
  if (!p.begin("display", false)) return;
  p.putUChar("panel", panel);
  p.putUChar("orient", orientation);
  p.end();
  gProfilePanel = panel;
  gProfileOrientation = orientation;
  Serial.printf("[config] screen is now %s %s\n", cfg::panelName(panel), cfg::orientationName(orientation));
}

void begin() {
  loadProfile();
  gActive = allocate();
  if (!gActive) {
    // Out of PSRAM: nothing sensible left to do, but do not crash in active().
    static cfg::Config fallback;
    gActive = &fallback;
    gActiveStatic = true;
  }
  // Partition label "spiffs" is LittleFS.begin()'s default; format a blank partition on first use.
  gMounted = LittleFS.begin(true);
  if (!gMounted) {
    snprintf(gWarning, sizeof(gWarning), "flash file system unavailable");
    Serial.println("[config] LittleFS mount failed, using the built-in default");
    useDefault(gActive);
    return;
  }
  LittleFS.remove(kTmpPath);  // left over from an interrupted save

  uint8_t* buf = nullptr;
  const size_t n = readFile(kPath, &buf);
  if (n == 0) {
    Serial.println("[config] no stored config, using the built-in default");
    useDefault(gActive);
    return;
  }
  gStoredCrc = proto::crc32(buf, n);
  char err[128];
  const bool ok = cfg::parse(reinterpret_cast<const char*>(buf), n, *gActive, err, sizeof(err));
  heap_caps_free(buf);
  if (ok) {
    Serial.printf("[config] loaded %s (%u bytes, crc %08lX, %u pages)\n", kPath, static_cast<unsigned>(n),
                  static_cast<unsigned long>(gStoredCrc), gActive->pageCount);
    rememberProfile(gActive->panel, gActive->orientation);
    return;
  }
  // Invalid stored config (should not happen: it was validated before saving). Keep the file and
  // its CRC, so the host sees a mismatch and pushes a good one.
  snprintf(gWarning, sizeof(gWarning), "stored config invalid: %s", err);
  Serial.printf("[config] %s, using the built-in default\n", gWarning);
  useDefault(gActive);
}

const cfg::Config& active() { return *gActive; }

const char* warning() { return gWarning; }

uint32_t storedCrc() { return gStoredCrc; }

cfg::Config* allocate() {
  void* mem = heap_caps_malloc(sizeof(cfg::Config), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return mem ? new (mem) cfg::Config() : nullptr;
}

void release(cfg::Config* c) {
  if (!c) return;
  c->~Config();
  heap_caps_free(c);
}

bool save(const uint8_t* json, size_t len, char* err, size_t errLen) {
  if (!gMounted) {
    snprintf(err, errLen, "flash file system unavailable");
    return false;
  }
  File f = LittleFS.open(kTmpPath, "w");
  if (!f) {
    snprintf(err, errLen, "cannot create %s", kTmpPath);
    return false;
  }
  const size_t written = f.write(json, len);
  f.close();
  // Read back and compare before replacing the old file.
  uint8_t* back = nullptr;
  const size_t n = readFile(kTmpPath, &back);
  const bool same = written == len && n == len && memcmp(back, json, len) == 0;
  if (back) heap_caps_free(back);
  if (!same) {
    LittleFS.remove(kTmpPath);
    snprintf(err, errLen, "flash write failed");
    return false;
  }
  // LittleFS renames atomically, replacing the old file.
  if (!LittleFS.rename(kTmpPath, kPath)) {
    LittleFS.remove(kTmpPath);
    snprintf(err, errLen, "rename failed");
    return false;
  }
  gStoredCrc = proto::crc32(json, len);
  return true;
}

void post(cfg::Config* c) {
  taskENTER_CRITICAL(&gLock);
  cfg::Config* old = gPosted;
  gPosted = c;
  taskEXIT_CRITICAL(&gLock);
  release(old);
}

cfg::Config* take() {
  taskENTER_CRITICAL(&gLock);
  cfg::Config* c = gPosted;
  gPosted = nullptr;
  taskEXIT_CRITICAL(&gLock);
  return c;
}

void setActive(cfg::Config* c) {
  cfg::Config* old = gActive;
  const bool oldStatic = gActiveStatic;
  gActive = c;
  gActiveStatic = false;
  gWarning[0] = '\0';
  if (!oldStatic) release(old);
}

size_t readStored(uint8_t* buf, size_t cap) {
  if (gMounted) {
    File f = LittleFS.open(kPath, "r");
    if (f) {
      const size_t n = f.size();
      const size_t got = n <= cap ? f.read(buf, n) : 0;
      f.close();
      if (n > 0 && got == n) return n;
    }
  }
  const size_t n = strlen(cfg::kDefaultConfigJson);
  if (n > cap) return 0;
  memcpy(buf, cfg::kDefaultConfigJson, n);
  return n;
}

bool erase() {
  if (!gMounted) return false;
  const bool ok = !LittleFS.exists(kPath) || LittleFS.remove(kPath);
  if (ok) gStoredCrc = 0;
  return ok;
}

}  // namespace config
}  // namespace rp
