#include "display.h"

#include <esp_heap_caps.h>

#include "pins.h"

namespace rp {
namespace display {

namespace {

class Lgfx : public lgfx::LGFX_Device {
 public:
  Lgfx() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = kDefaultSpiHz;
      cfg.freq_read = 16000000;  // unused: MISO is not connected
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = pins::kTftSck;
      cfg.pin_mosi = pins::kTftMosi;
      cfg.pin_miso = pins::kTftMiso;  // -1: ILI9488 SDO does not tri-state
      cfg.pin_dc = pins::kTftDc;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = pins::kTftCs;
      cfg.pin_rst = pins::kTftRst;
      cfg.pin_busy = -1;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      panel_.config(cfg);
    }
    setPanel(&panel_);
  }

  void setWriteClock(uint32_t hz) { bus_.setClock(hz); }

 private:
  lgfx::Panel_ILI9488 panel_;
  lgfx::Bus_SPI bus_;
};

Lgfx gLcd;
uint32_t gSpiHz = kDefaultSpiHz;

constexpr uint32_t kSpiSourceHz = 80000000;  // APB clock feeding SPI2 on the ESP32-S3

uint16_t* allocDma(size_t pixels) {
  return static_cast<uint16_t*>(heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
}

void fillPattern(uint16_t* buf, int w, int h, uint32_t seed) {
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      buf[y * w + x] = static_cast<uint16_t>((x * 31 + y * 17 + seed) * 2654435761u >> 16);
    }
  }
}

}  // namespace

bool begin(uint32_t spiHz) {
  gSpiHz = spiHz;
  gLcd.setWriteClock(spiHz);
  const bool ok = gLcd.init();
  gLcd.setRotation(kDefaultRotation);
  gLcd.setColorDepth(16);  // RGB565 in, LovyanGFX converts to the panel's RGB666
  gLcd.fillScreen(TFT_BLACK);
  return ok;
}

lgfx::LGFX_Device& lcd() { return gLcd; }

void setSpiHz(uint32_t hz) {
  gSpiHz = hz;
  gLcd.setWriteClock(hz);
}

uint32_t spiHz() { return gSpiHz; }

uint32_t effectiveSpiHz(uint32_t requestedHz) {
  if (requestedHz >= kSpiSourceHz) return kSpiSourceHz;
  if (requestedHz == 0) return 0;
  const uint32_t n = kSpiSourceHz / (requestedHz + 1);  // same rounding as LovyanGFX FreqToClockDiv
  return kSpiSourceHz / (n + 1);
}

void setRotation(uint8_t r) { gLcd.setRotation(r & 3); }

void setInvert(bool on) { gLcd.invertDisplay(on); }

void drawTestPattern() {
  auto& d = gLcd;
  const int w = d.width();
  const int h = d.height();
  d.startWrite();
  d.fillScreen(TFT_BLACK);

  // 1 px white border: every edge must be visible, none cut off or doubled.
  d.drawRect(0, 0, w, h, TFT_WHITE);

  // Corner markers to check rotation and mirroring.
  const int s = 36;
  d.fillRect(2, 2, s, s, TFT_RED);
  d.fillRect(w - s - 2, 2, s, s, TFT_GREEN);
  d.fillRect(2, h - s - 2, s, s, TFT_BLUE);
  d.fillRect(w - s - 2, h - s - 2, s, s, TFT_WHITE);
  d.setTextDatum(lgfx::middle_center);
  d.setTextSize(1);
  d.setTextColor(TFT_WHITE);
  d.drawString("R", 2 + s / 2, 2 + s / 2);
  d.setTextColor(TFT_BLACK);
  d.drawString("G", w - s / 2 - 2, 2 + s / 2);
  d.setTextColor(TFT_WHITE);
  d.drawString("B", 2 + s / 2, h - s / 2 - 2);
  d.setTextColor(TFT_BLACK);
  d.drawString("W", w - s / 2 - 2, h - s / 2 - 2);

  // Eight color bars.
  static const uint32_t kBars[] = {TFT_WHITE, TFT_YELLOW, TFT_CYAN, TFT_GREEN,
                                   TFT_MAGENTA, TFT_RED, TFT_BLUE, TFT_BLACK};
  const int barTop = 48;
  const int barH = 70;
  const int barX0 = 48;
  const int barW = (w - 2 * barX0) / 8;
  for (int i = 0; i < 8; ++i) d.fillRect(barX0 + i * barW, barTop, barW, barH, kBars[i]);
  d.drawRect(barX0 - 1, barTop - 1, barW * 8 + 2, barH + 2, TFT_DARKGREY);

  // Red, green, blue and grey ramps: smooth bands mean color depth is right.
  const int rampTop = barTop + barH + 10;
  const int rampH = 18;
  for (int x = 0; x < w - 2 * barX0; ++x) {
    const uint8_t v = static_cast<uint8_t>(x * 255 / (w - 2 * barX0 - 1));
    d.drawFastVLine(barX0 + x, rampTop, rampH, d.color888(v, 0, 0));
    d.drawFastVLine(barX0 + x, rampTop + rampH, rampH, d.color888(0, v, 0));
    d.drawFastVLine(barX0 + x, rampTop + 2 * rampH, rampH, d.color888(0, 0, v));
    d.drawFastVLine(barX0 + x, rampTop + 3 * rampH, rampH, d.color888(v, v, v));
  }

  // Fine 1 px checkerboard: SPI glitches show up as speckles or shifted rows here.
  const int chkTop = rampTop + 4 * rampH + 10;
  const int chkH = h - chkTop - 44;
  for (int y = 0; y < chkH; ++y) {
    for (int x = 0; x < 120; ++x) {
      d.writePixel(barX0 + x, chkTop + y, ((x ^ y) & 1) ? TFT_WHITE : TFT_BLACK);
    }
  }

  // Text, including extended Latin to check later font work against (built-in font is ASCII only).
  d.setTextDatum(lgfx::top_left);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setTextSize(2);
  d.drawString("rez-pad", barX0 + 135, chkTop);
  d.setTextSize(1);
  char line[64];
  snprintf(line, sizeof(line), "%dx%d  rot %u", w, h, d.getRotation());
  d.drawString(line, barX0 + 135, chkTop + 22);
  snprintf(line, sizeof(line), "SPI %.1f MHz (req %.1f)", effectiveSpiHz(gSpiHz) / 1e6, gSpiHz / 1e6);
  d.drawString(line, barX0 + 135, chkTop + 34);
  d.endWrite();
}

void runBenchmark(const uint32_t* hzList, size_t count, Print& out) {
  constexpr int kRegion = 200;
  constexpr int kStripH = 40;
  constexpr int kRuns = 5;

  uint16_t* region = allocDma(kRegion * kRegion);
  uint16_t* strip = allocDma(480 * kStripH);
  if (!region || !strip) {
    out.println("bench: not enough internal DMA RAM");
    heap_caps_free(region);
    heap_caps_free(strip);
    return;
  }
  fillPattern(region, kRegion, kRegion, 1);
  fillPattern(strip, 480, kStripH, 2);

  auto& d = gLcd;
  const uint32_t savedHz = gSpiHz;
  const int w = d.width();
  const int h = d.height();
  const double bitsPerFrame = static_cast<double>(w) * h * 3 * 8;  // RGB666 on the wire

  out.println("bench: average of 5 runs, times in ms");
  out.println("  req MHz  eff MHz  fill  ideal  region200  strips480x40  fps(strips)");
  for (size_t i = 0; i < count; ++i) {
    setSpiHz(hzList[i]);
    const uint32_t eff = effectiveSpiHz(hzList[i]);

    // Warm up so the new clock divider is applied before timing.
    d.startWrite();
    d.fillRect(0, 0, 1, 1, TFT_BLACK);
    d.endWrite();

    uint32_t t0 = micros();
    for (int r = 0; r < kRuns; ++r) {
      d.startWrite();
      d.fillScreen(r & 1 ? TFT_NAVY : TFT_DARKGREEN);
      d.endWrite();
    }
    const float fillMs = (micros() - t0) / 1000.0f / kRuns;

    t0 = micros();
    for (int r = 0; r < kRuns; ++r) {
      d.startWrite();
      d.pushImageDMA((w - kRegion) / 2, (h - kRegion) / 2, kRegion, kRegion, region);
      d.endWrite();
    }
    d.waitDMA();
    const float regionMs = (micros() - t0) / 1000.0f / kRuns;

    t0 = micros();
    for (int r = 0; r < kRuns; ++r) {
      d.startWrite();
      for (int y = 0; y < h; y += kStripH) {
        d.pushImageDMA(0, y, w, kStripH, strip);
      }
      d.endWrite();
    }
    d.waitDMA();
    const float stripsMs = (micros() - t0) / 1000.0f / kRuns;

    const float idealMs = static_cast<float>(bitsPerFrame / eff * 1000.0);
    out.printf("  %7.1f  %7.1f  %5.1f  %5.1f  %9.1f  %12.1f  %11.1f\n", hzList[i] / 1e6, eff / 1e6,
               fillMs, idealMs, regionMs, stripsMs, 1000.0f / stripsMs);
  }

  heap_caps_free(region);
  heap_caps_free(strip);
  setSpiHz(savedHz);
  drawTestPattern();
  out.printf("bench: done, clock back to %.1f MHz\n", effectiveSpiHz(savedHz) / 1e6);
}

}  // namespace display
}  // namespace rp
