#include "debug_view.h"

#include <Arduino.h>
#include <string.h>

#include "display/display.h"

namespace rp {
namespace debug_view {

namespace {

constexpr int kLineH = 18;
constexpr int kLeftX = 8;
constexpr int kRightX = 250;
constexpr int kColW = 226;
constexpr int kTop = 30;
constexpr int kEventLines = 7;
constexpr uint32_t kBg = TFT_BLACK;
constexpr uint32_t kFg = TFT_WHITE;
constexpr uint32_t kDim = 0x8410;    // grey
constexpr uint32_t kAccent = 0x4E1E;  // light blue, close to the default theme accent

// A text field that redraws only when its content changes.
struct Field {
  int x, y, w;
  char text[48];
};

enum FieldId {
  kUptime,
  kEnc1, kEnc1Sw, kEnc2, kEnc2Sw, kBtns, kDropped,
  kEv0,  // kEventLines fields
  kTemp = kEv0 + kEventLines, kHum, kPress, kTrend, kBmpT, kRaw, kSamples, kErrors, kOffsets,
  kFieldCount
};

Field gFields[kFieldCount];
bool gActive = false;
char gEvents[kEventLines][48];
int gEventCount = 0;
bool gEventsDirty = false;

void initField(FieldId id, int x, int y, int w) { gFields[id] = {x, y, w, {0}}; }

void setField(FieldId id, const char* text, uint32_t color = kFg) {
  Field& f = gFields[id];
  if (strncmp(f.text, text, sizeof(f.text)) == 0) return;
  strncpy(f.text, text, sizeof(f.text) - 1);
  f.text[sizeof(f.text) - 1] = '\0';
  auto& d = display::lcd();
  d.fillRect(f.x, f.y, f.w, kLineH, kBg);
  d.setTextColor(color, kBg);
  d.drawString(f.text, f.x, f.y + 1);
}

void label(const char* text, int x, int y, uint32_t color = kDim) {
  auto& d = display::lcd();
  d.setTextColor(color, kBg);
  d.drawString(text, x, y + 1);
}

}  // namespace

void show() {
  auto& d = display::lcd();
  d.startWrite();
  d.fillScreen(kBg);
  d.setFont(&fonts::Font2);
  d.setTextDatum(lgfx::top_left);
  d.fillRect(0, 0, d.width(), 24, 0x18E3);
  d.setTextColor(kFg, 0x18E3);
  d.drawString("rez-pad  debug view (Phase 2)", kLeftX, 4);
  d.drawFastVLine(240, kTop, d.height() - kTop - 6, 0x39E7);

  label("INPUTS", kLeftX, kTop, kAccent);
  label("ROOM", kRightX, kTop, kAccent);

  int y = kTop + kLineH + 4;
  initField(kEnc1, kLeftX, y, kColW);
  initField(kEnc1Sw, kLeftX, y + kLineH, kColW);
  initField(kEnc2, kLeftX, y + 2 * kLineH, kColW);
  initField(kEnc2Sw, kLeftX, y + 3 * kLineH, kColW);
  initField(kBtns, kLeftX, y + 4 * kLineH, kColW);
  initField(kDropped, kLeftX, y + 5 * kLineH, kColW);
  label("events (newest first)", kLeftX, y + 6 * kLineH + 4, kAccent);
  for (int i = 0; i < kEventLines; ++i) {
    initField(static_cast<FieldId>(kEv0 + i), kLeftX, y + (7 + i) * kLineH + 4, kColW);
  }

  initField(kTemp, kRightX, y, kColW);
  initField(kHum, kRightX, y + kLineH, kColW);
  initField(kPress, kRightX, y + 2 * kLineH, kColW);
  initField(kTrend, kRightX, y + 3 * kLineH, kColW);
  initField(kBmpT, kRightX, y + 5 * kLineH, kColW);
  initField(kRaw, kRightX, y + 6 * kLineH, kColW);
  initField(kSamples, kRightX, y + 7 * kLineH, kColW);
  initField(kErrors, kRightX, y + 8 * kLineH, kColW);
  initField(kOffsets, kRightX, y + 9 * kLineH, kColW);
  initField(kUptime, d.width() - 110, 4, 102);
  // Uptime lives on the header bar; give it the header color.
  d.endWrite();

  gActive = true;
  gEventsDirty = true;
}

bool active() { return gActive; }
void hide() { gActive = false; }

void addEvent(const input::Event& e) {
  for (int i = kEventLines - 1; i > 0; --i) memcpy(gEvents[i], gEvents[i - 1], sizeof(gEvents[i]));
  if (e.kind == input::Kind::kRotate || e.kind == input::Kind::kRotatePressed) {
    snprintf(gEvents[0], sizeof(gEvents[0]), "%7.2fs  %s %s %+d", e.timeMs / 1000.0f,
             input::sourceName(e.source), input::kindName(e.kind), e.value);
  } else {
    snprintf(gEvents[0], sizeof(gEvents[0]), "%7.2fs  %s %s", e.timeMs / 1000.0f, input::sourceName(e.source),
             input::kindName(e.kind));
  }
  if (gEventCount < kEventLines) ++gEventCount;
  gEventsDirty = true;
}

void update(const input::Snapshot& in, const sensors::Reading& r) {
  if (!gActive) return;
  auto& d = display::lcd();
  char buf[48];
  d.startWrite();

  {
    // Uptime on the header bar (own colors, so draw directly).
    const uint32_t s = millis() / 1000;
    snprintf(buf, sizeof(buf), "up %02lu:%02lu:%02lu", static_cast<unsigned long>(s / 3600),
             static_cast<unsigned long>((s / 60) % 60), static_cast<unsigned long>(s % 60));
    Field& f = gFields[kUptime];
    if (strcmp(f.text, buf) != 0) {
      strcpy(f.text, buf);
      d.fillRect(f.x, 0, f.w, 24, 0x18E3);
      d.setTextColor(kFg, 0x18E3);
      d.drawString(buf, f.x, 4);
    }
  }

  // By physical port (knob ports 1 and 2; numbering is in the config).
  for (int i = 0; i < 2; ++i) {
    snprintf(buf, sizeof(buf), "knob port %d  count %ld  detents %ld", i + 1,
             static_cast<long>(in.rawCount[i]), static_cast<long>(in.detents[i]));
    setField(i == 0 ? kEnc1 : kEnc2, buf);
    snprintf(buf, sizeof(buf), "      switch %s", in.switchDown[i] ? "DOWN" : "up");
    setField(i == 0 ? kEnc1Sw : kEnc2Sw, buf, in.switchDown[i] ? TFT_YELLOW : kFg);
  }
  snprintf(buf, sizeof(buf), "button ports  1 %s  2 %s  3 %s  4 %s", in.buttonDown[0] ? "DOWN" : "up",
           in.buttonDown[1] ? "DOWN" : "up", in.buttonDown[2] ? "DOWN" : "up", in.buttonDown[3] ? "DOWN" : "up");
  setField(kBtns, buf);
  snprintf(buf, sizeof(buf), "queue drops %lu", static_cast<unsigned long>(in.dropped));
  setField(kDropped, buf, in.dropped ? TFT_RED : kDim);

  if (gEventsDirty) {
    for (int i = 0; i < kEventLines; ++i) {
      setField(static_cast<FieldId>(kEv0 + i), i < gEventCount ? gEvents[i] : "", i == 0 ? kFg : kDim);
    }
    gEventsDirty = false;
  }

  if (r.updatedMs == 0) {
    setField(kTemp, "waiting for first reading...", kDim);
  } else {
    if (r.ahtOk || r.samples > 0) {
      snprintf(buf, sizeof(buf), "temp   %.1f C", r.tempC);
      setField(kTemp, buf, r.ahtOk ? kFg : TFT_RED);
      snprintf(buf, sizeof(buf), "hum    %.1f %%", r.humPct);
      setField(kHum, buf, r.ahtOk ? kFg : TFT_RED);
    }
    if (r.bmpAddr) {
      snprintf(buf, sizeof(buf), "press  %.1f hPa", r.pressHpa);
      setField(kPress, buf, r.bmpOk ? kFg : TFT_RED);
    } else {
      setField(kPress, "press  BMP280 not found", TFT_RED);
    }
    if (r.trend == sensor::Trend::kUnknown) {
      setField(kTrend, "trend  collecting (needs 1 h)", kDim);
    } else {
      snprintf(buf, sizeof(buf), "trend  %s (%+.1f hPa/3h)", sensor::trendName(r.trend), r.trendDelta3h);
      setField(kTrend, buf);
    }
    snprintf(buf, sizeof(buf), "bmp280 temp %.1f C (0x%02X)", r.bmpTempC, r.bmpAddr);
    setField(kBmpT, buf, kDim);
    snprintf(buf, sizeof(buf), "raw  %.2f C  %.1f %%  %.2f", r.rawTempC, r.rawHumPct, r.rawPressHpa);
    setField(kRaw, buf, kDim);
    snprintf(buf, sizeof(buf), "samples %lu, age %lus", static_cast<unsigned long>(r.samples),
             static_cast<unsigned long>((millis() - r.updatedMs) / 1000));
    setField(kSamples, buf, kDim);
  }
  snprintf(buf, sizeof(buf), "errors aht %lu  bmp %lu", static_cast<unsigned long>(r.ahtErrors),
           static_cast<unsigned long>(r.bmpErrors));
  setField(kErrors, buf, (r.ahtErrors || r.bmpErrors) ? TFT_ORANGE : kDim);
  const auto sc = sensors::config();
  snprintf(buf, sizeof(buf), "offsets t %+.1f C  h %+.1f %%", sc.tempOffsetC, sc.humOffsetPct);
  setField(kOffsets, buf, kDim);

  d.endWrite();
}

}  // namespace debug_view
}  // namespace rp
