#include "spectrum.h"

#include <freertos/FreeRTOS.h>
#include <string.h>

namespace rp {
namespace spectrum {

namespace {

constexpr uint32_t kStaleMs = 300;  // no frame for this long: fall by ourselves
constexpr uint32_t kFallStepMs = 33;
constexpr uint32_t kFallFullMs = 600;  // same fall rate as the host's smoothing

portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED;
ui::Spectrum gFrame;
bool gNew = false;
uint32_t gReceived = 0;
uint32_t gLastFrameMs = 0;
uint32_t gLastFallMs = 0;

}  // namespace

void onSpectrum(const uint8_t* body, size_t len) {
  if (len < 1) return;
  const uint8_t n = body[0];
  if (n < 1 || n > 64 || len < 1u + n) return;
  ui::Spectrum f;
  f.n = n > ui::kSpectrumBins ? ui::kSpectrumBins : n;
  memcpy(f.bins, body + 1, f.n);
  taskENTER_CRITICAL(&gLock);
  gFrame = f;
  gNew = true;
  taskEXIT_CRITICAL(&gLock);
  gReceived++;
}

bool update(ui::Spectrum& s, uint32_t nowMs) {
  taskENTER_CRITICAL(&gLock);
  const bool fresh = gNew;
  if (fresh) {
    s = gFrame;
    gNew = false;
  }
  taskEXIT_CRITICAL(&gLock);
  if (fresh) {
    gLastFrameMs = nowMs;
    return true;
  }
  if (nowMs - gLastFrameMs < kStaleMs || nowMs - gLastFallMs < kFallStepMs) return false;
  const uint32_t dt = nowMs - gLastFallMs > 100 ? kFallStepMs : nowMs - gLastFallMs;
  gLastFallMs = nowMs;
  const int step = static_cast<int>(255 * dt / kFallFullMs) + 1;
  bool any = false;
  for (int i = 0; i < s.n; ++i) {
    if (s.bins[i] == 0) continue;
    s.bins[i] = static_cast<uint8_t>(s.bins[i] > step ? s.bins[i] - step : 0);
    any = true;
  }
  return any;
}

uint32_t received() { return gReceived; }

}  // namespace spectrum
}  // namespace rp
