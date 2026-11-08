#include "media.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <JPEGDEC.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <string.h>
#include <sys/time.h>

#include <new>

#include "art_assembler.h"
#include "link/link.h"

namespace rp {
namespace media {

namespace {

constexpr size_t kMaxJpegBytes = 64 * 1024;  // a 200 px JPEG at quality 85 is about 8 to 20 KB
constexpr int32_t kMaxClockSkewMs = 10000;

portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED;

// NOW_PLAYING / VOLUME mailboxes (latest wins).
ui::NowPlaying gNp;
bool gNpNew = false;
ui::Volume gVol;
bool gVolNew = false;

// Art: assembled JPEG -> decoded into one of two pixel buffers.
uint8_t* gJpegBuf = nullptr;
proto::ArtAssembler* gAsm = nullptr;
JPEGDEC* gJpeg = nullptr;
uint16_t* gPix[2] = {nullptr, nullptr};
int8_t gShown = -1;  // buffer the UI displays (-1 none); never written by the link task
struct Pending {
  bool has = false;
  uint32_t id = 0;
  int8_t buf = -1;  // -1 = the track has no art
  uint16_t w = 0, h = 0;
} gPending;
volatile bool gTransfer = false;
volatile int gArtSize = 0;
uint16_t gBeginW = 0, gBeginH = 0;

Stats gStats = {};

// Copies UTF-8 text, cutting at a character boundary when it does not fit.
void copyUtf8(char* dst, size_t cap, const char* src) {
  size_t n = strlen(src);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && (static_cast<uint8_t>(src[n]) & 0xC0) == 0x80) --n;  // src[n] continues a character
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

int64_t unixMsNow() {
  timeval tv;
  gettimeofday(&tv, nullptr);
  return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

ui::PlayState parseState(const char* s) {
  if (strcmp(s, "playing") == 0) return ui::PlayState::kPlaying;
  if (strcmp(s, "paused") == 0) return ui::PlayState::kPaused;
  if (strcmp(s, "stopped") == 0) return ui::PlayState::kStopped;
  return ui::PlayState::kNone;
}

bool parse(JsonDocument& doc, const uint8_t* body, size_t len) {
  if (deserializeJson(doc, body, len) == DeserializationError::Ok) return true;
  gStats.badJson++;
  return false;
}

void publishArt(uint32_t id, int8_t buf, uint16_t w, uint16_t h) {
  taskENTER_CRITICAL(&gLock);
  gPending.has = true;
  gPending.id = id;
  gPending.buf = buf;
  gPending.w = w;
  gPending.h = h;
  taskEXIT_CRITICAL(&gLock);
}

// ---- JPEG decode ----

struct DecodeTarget {
  uint16_t* px;
  int w, h;
};

int drawBlock(JPEGDRAW* d) {
  const auto* t = static_cast<const DecodeTarget*>(d->pUser);
  if (d->x >= t->w || d->y >= t->h) return 1;
  const int cols = d->x + d->iWidth > t->w ? t->w - d->x : d->iWidth;
  const int rows = d->y + d->iHeight > t->h ? t->h - d->y : d->iHeight;
  for (int r = 0; r < rows; ++r) {
    memcpy(t->px + (d->y + r) * t->w + d->x, d->pPixels + r * d->iWidth, cols * sizeof(uint16_t));
  }
  return 1;
}

// Decodes the assembled JPEG into a free buffer and hands it to the UI.
void decodeAndPublish(uint32_t id) {
  // Pick the buffer the UI is not showing. If an untaken image already sits there, it is
  // replaced; clear it first so the UI cannot switch to a buffer that is being written.
  int8_t target;
  taskENTER_CRITICAL(&gLock);
  target = gShown == 0 ? 1 : 0;
  if (gPending.has && gPending.buf == target) gPending.has = false;
  taskEXIT_CRITICAL(&gLock);

  const uint32_t t0 = micros();
  bool ok = gJpeg->openRAM(const_cast<uint8_t*>(gAsm->data()), static_cast<int>(gAsm->size()), drawBlock) != 0;
  int w = 0, h = 0;
  if (ok) {
    w = gJpeg->getWidth();
    h = gJpeg->getHeight();
    ok = w > 0 && h > 0 && w <= kMaxArtSide && h <= kMaxArtSide;
    if (ok) {
      DecodeTarget t{gPix[target], w, h};
      gJpeg->setUserPointer(&t);
      gJpeg->setPixelType(RGB565_LITTLE_ENDIAN);  // LVGL's native RGB565 byte order
      ok = gJpeg->decode(0, 0, 0) != 0;
    }
    gJpeg->close();
  }
  gStats.lastDecodeUs = micros() - t0;
  if (!ok) {
    gStats.artFailed++;
    Serial.printf("[media] art %08lX: JPEG decode failed (%dx%d, error %d)\n", static_cast<unsigned long>(id), w, h,
                  gJpeg->getLastError());
    publishArt(id, -1, 0, 0);
    return;
  }
  gStats.artOk++;
  publishArt(id, target, static_cast<uint16_t>(w), static_cast<uint16_t>(h));
  Serial.printf("[media] art %08lX: %u bytes, %dx%d, decoded in %lu us\n", static_cast<unsigned long>(id),
                static_cast<unsigned>(gAsm->size()), w, h, static_cast<unsigned long>(gStats.lastDecodeUs));
}

}  // namespace

bool begin() {
  const size_t pixBytes = kMaxArtSide * kMaxArtSide * sizeof(uint16_t);
  gJpegBuf = static_cast<uint8_t*>(heap_caps_malloc(kMaxJpegBytes, MALLOC_CAP_SPIRAM));
  gPix[0] = static_cast<uint16_t*>(heap_caps_malloc(pixBytes, MALLOC_CAP_SPIRAM));
  gPix[1] = static_cast<uint16_t*>(heap_caps_malloc(pixBytes, MALLOC_CAP_SPIRAM));
  // The decoder's working buffers are hit hard while decoding: keep them in internal RAM.
  void* jpegMem = heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!jpegMem) jpegMem = heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM);
  if (!gJpegBuf || !gPix[0] || !gPix[1] || !jpegMem) return false;
  gJpeg = new (jpegMem) JPEGDEC();
  static proto::ArtAssembler assembler(gJpegBuf, kMaxJpegBytes);
  gAsm = &assembler;
  Serial.printf("[media] JPEG decoder %u bytes, art buffers 2 x %u KB in PSRAM\n",
                static_cast<unsigned>(sizeof(JPEGDEC)), static_cast<unsigned>(pixBytes / 1024));
  return true;
}

// ---- link task ----

void onNowPlaying(const uint8_t* body, size_t len) {
  JsonDocument doc;
  if (!parse(doc, body, len)) return;
  ui::NowPlaying np;
  np.state = parseState(doc["state"] | "none");
  np.trackId = doc["track_id"] | 0u;
  copyUtf8(np.title, sizeof(np.title), doc["title"] | "");
  copyUtf8(np.artist, sizeof(np.artist), doc["artist"] | "");
  copyUtf8(np.album, sizeof(np.album), doc["album"] | "");
  copyUtf8(np.app, sizeof(np.app), doc["app"] | "");
  const int64_t pos = doc["pos_ms"] | static_cast<int64_t>(0);
  const int64_t dur = doc["dur_ms"] | static_cast<int64_t>(0);
  const int64_t ts = doc["ts"] | static_cast<int64_t>(0);
  np.posMs = pos > 0 ? static_cast<uint32_t>(pos) : 0;
  np.durMs = dur > 0 ? static_cast<uint32_t>(dur) : 0;
  // Position was sampled at host time `ts`; translate to device millis() with the synced clock.
  int32_t age = 0;
  if (link::timeSynced() && ts > 0) {
    const int64_t a = unixMsNow() - ts;
    age = a < 0 ? 0 : (a > kMaxClockSkewMs ? kMaxClockSkewMs : static_cast<int32_t>(a));
  }
  np.sampledAtMs = millis() - static_cast<uint32_t>(age);
  np.canSeek = doc["can_seek"] | false;
  np.canNext = doc["can_next"] | false;
  np.canPrev = doc["can_prev"] | false;

  taskENTER_CRITICAL(&gLock);
  gNp = np;
  gNpNew = true;
  taskEXIT_CRITICAL(&gLock);
  gStats.nowPlaying++;
}

void onVolume(const uint8_t* body, size_t len) {
  JsonDocument doc;
  if (!parse(doc, body, len)) return;
  ui::Volume v;
  const int level = doc["level"] | 0;
  v.level = static_cast<uint8_t>(level < 0 ? 0 : (level > 100 ? 100 : level));
  v.muted = doc["muted"] | false;
  v.valid = true;
  JsonObject app = doc["app"];
  if (!app.isNull()) {
    const int al = app["level"] | 0;
    v.appValid = true;
    v.appLevel = static_cast<uint8_t>(al < 0 ? 0 : (al > 100 ? 100 : al));
    v.appMuted = app["muted"] | false;
    strlcpy(v.appName, app["name"] | "", sizeof(v.appName));
  }
  taskENTER_CRITICAL(&gLock);
  gVol = v;
  gVolNew = true;
  taskEXIT_CRITICAL(&gLock);
}

void onArtBegin(const uint8_t* body, size_t len) {
  JsonDocument doc;
  if (!parse(doc, body, len) || !gAsm) return;
  const uint32_t id = doc["id"] | 0u;
  const uint32_t size = doc["size"] | 0u;
  const uint32_t crc = doc["crc32"] | 0u;
  const char* fmt = doc["fmt"] | "";
  gBeginW = doc["w"] | 0;
  gBeginH = doc["h"] | 0;
  if (strcmp(fmt, "jpeg") != 0 || gBeginW > kMaxArtSide || gBeginH > kMaxArtSide ||
      gAsm->begin(id, size, crc) != proto::ArtAssembler::Result::kOk) {
    Serial.printf("[media] art %08lX rejected: %s %ux%u, %lu bytes\n", static_cast<unsigned long>(id), fmt, gBeginW,
                  gBeginH, static_cast<unsigned long>(size));
    gAsm->abort();
    gStats.artFailed++;
    publishArt(id, -1, 0, 0);
    gTransfer = false;
    return;
  }
  gTransfer = true;
}

void onArtChunk(const uint8_t* body, size_t len) {
  if (!gAsm) return;
  if (gAsm->chunk(body, len) == proto::ArtAssembler::Result::kBadChunk) {
    Serial.printf("[media] art %08lX: bad chunk, transfer dropped\n", static_cast<unsigned long>(gAsm->id()));
    gStats.artFailed++;
    gTransfer = false;
  }
}

void onArtEnd(const uint8_t* body, size_t len) {
  JsonDocument doc;
  if (!parse(doc, body, len) || !gAsm) return;
  const uint32_t id = doc["id"] | 0u;
  const auto r = gAsm->end(id);
  gTransfer = gAsm->active();
  if (r == proto::ArtAssembler::Result::kIgnored) return;
  gStats.lastArtBytes = gAsm->size();
  if (r != proto::ArtAssembler::Result::kOk) {
    Serial.printf("[media] art %08lX: %s (%u of %u bytes)\n", static_cast<unsigned long>(id),
                  r == proto::ArtAssembler::Result::kBadCrc ? "CRC mismatch" : "incomplete",
                  static_cast<unsigned>(gAsm->received()), static_cast<unsigned>(gAsm->size()));
    gStats.artFailed++;
    return;  // the UI asks for it again (ART_REQUEST) if it still needs it
  }
  decodeAndPublish(id);
}

void onArtNone(const uint8_t* body, size_t len) {
  JsonDocument doc;
  if (!parse(doc, body, len)) return;
  publishArt(doc["id"] | 0u, -1, 0, 0);
}

void onHostLost() {
  if (gAsm) gAsm->abort();
  gTransfer = false;
}

// ---- UI task ----

bool takeNowPlaying(ui::NowPlaying& out) {
  taskENTER_CRITICAL(&gLock);
  const bool fresh = gNpNew;
  if (fresh) {
    out = gNp;
    gNpNew = false;
  }
  taskEXIT_CRITICAL(&gLock);
  return fresh;
}

bool takeVolume(ui::Volume& out) {
  taskENTER_CRITICAL(&gLock);
  const bool fresh = gVolNew;
  if (fresh) {
    out = gVol;
    gVolNew = false;
  }
  taskEXIT_CRITICAL(&gLock);
  return fresh;
}

bool takeArt(ui::Art& out) {
  taskENTER_CRITICAL(&gLock);
  const bool fresh = gPending.has;
  if (fresh) {
    gPending.has = false;
    gShown = gPending.buf;
    out.id = gPending.id;
    out.w = gPending.w;
    out.h = gPending.h;
    out.px = gPending.buf >= 0 ? gPix[gPending.buf] : nullptr;
  }
  taskEXIT_CRITICAL(&gLock);
  return fresh;
}

void releaseArt() {
  taskENTER_CRITICAL(&gLock);
  gShown = -1;
  gPending.has = false;
  taskEXIT_CRITICAL(&gLock);
}

bool artTransferActive() { return gTransfer; }

void setArtSize(int px) { gArtSize = px; }

int artSize() { return gArtSize; }

Stats stats() { return gStats; }

}  // namespace media
}  // namespace rp
