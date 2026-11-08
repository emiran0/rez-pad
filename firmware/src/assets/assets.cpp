#include "assets.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "asset_format.h"
#include "ui/theme.h"

namespace rp {
namespace assets {

namespace {

constexpr const char* kDir = "/a";
volatile uint32_t gGeneration = 0;

void path(char* out, size_t n, const char* name) { snprintf(out, n, "%s/%s", kDir, name); }

}  // namespace

bool validName(const char* name) {
  const size_t n = name ? strlen(name) : 0;
  if (n < 5 || n >= proto::kAssetNameMax || strcmp(name + n - 4, ".rpa") != 0) return false;
  for (size_t i = 0; i < n; ++i) {
    const char c = name[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '_' || c == '.')) return false;
  }
  return true;
}

bool save(const char* name, const uint8_t* data, size_t len, char* err, size_t errLen) {
  if (!validName(name)) {
    snprintf(err, errLen, "bad asset name");
    return false;
  }
  proto::RpaReader r;
  if (!r.open(data, len)) {
    snprintf(err, errLen, "not an RPA1 file");
    return false;
  }
  if (!LittleFS.exists(kDir)) LittleFS.mkdir(kDir);
  char tmp[64], dst[64];
  path(tmp, sizeof(tmp), "upload.tmp");
  path(dst, sizeof(dst), name);
  File f = LittleFS.open(tmp, "w");
  if (!f) {
    snprintf(err, errLen, "cannot create file (flash full?)");
    return false;
  }
  const size_t written = f.write(data, len);
  f.close();
  if (written != len || !LittleFS.rename(tmp, dst)) {
    LittleFS.remove(tmp);
    snprintf(err, errLen, "flash write failed (%u of %u bytes)", static_cast<unsigned>(written),
             static_cast<unsigned>(len));
    return false;
  }
  gGeneration = gGeneration + 1;
  return true;
}

size_t load(const char* name, uint8_t** out) {
  *out = nullptr;
  char p[64];
  path(p, sizeof(p), name);
  File f = LittleFS.open(p, "r");
  if (!f) return 0;
  const size_t n = f.size();
  uint8_t* buf = n ? static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) : nullptr;
  const size_t got = buf ? f.read(buf, n) : 0;
  f.close();
  if (!buf || got != n) {
    if (buf) heap_caps_free(buf);
    return 0;
  }
  *out = buf;
  return n;
}

void list(JsonDocument& out) {
  JsonArray files = out["files"].to<JsonArray>();
  File dir = LittleFS.open(kDir);
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      JsonObject o = files.add<JsonObject>();
      o["name"] = f.name();
      o["size"] = f.size();
    }
  }
  out["free"] = LittleFS.totalBytes() - LittleFS.usedBytes();
}

bool nameFor(const cfg::WidgetSpec& w, int boxW, int boxH, uint32_t bg, char* out, size_t n) {
  return proto::assetName(out, n, w.asset, boxW, boxH, w.fit == cfg::Fit::kContain, bg);
}

void collectGarbage(const cfg::Config& c) {
  File dir = LittleFS.open(kDir);
  if (!dir || !dir.isDirectory()) return;
  // Names the config needs (at most 8 pages x 16 widgets), in PSRAM.
  using Name = char[proto::kAssetNameMax];
  Name* used = static_cast<Name*>(
      heap_caps_malloc(sizeof(Name) * cfg::kMaxPages * cfg::kMaxWidgets, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!used) return;
  int count = 0;
  for (int p = 0; p < c.pageCount; ++p) {
    const cfg::Page& page = c.pages[p];
    const uint32_t bg = ui::makePalette(page.theme, page.accent).card;
    for (int i = 0; i < page.widgetCount; ++i) {
      const cfg::WidgetSpec& w = page.widgets[i];
      if (strcmp(w.type, "image") != 0) continue;
      if (nameFor(w, cfg::cardW(w.w, page.cols) - 2, cfg::cardH(w.h, page.rows) - 2, bg, used[count], proto::kAssetNameMax)) {
        ++count;
      }
    }
  }
  char victims[16][proto::kAssetNameMax];
  int nv = 0;
  for (File f = dir.openNextFile(); f && nv < 16; f = dir.openNextFile()) {
    bool keep = false;
    for (int i = 0; i < count && !keep; ++i) keep = strcmp(used[i], f.name()) == 0;
    if (!keep) snprintf(victims[nv++], proto::kAssetNameMax, "%s", f.name());
  }
  dir.close();
  heap_caps_free(used);
  for (int i = 0; i < nv; ++i) {
    char p[64];
    path(p, sizeof(p), victims[i]);
    LittleFS.remove(p);
    Serial.printf("[assets] deleted unused %s\n", victims[i]);
  }
}

uint32_t generation() { return gGeneration; }

}  // namespace assets
}  // namespace rp
