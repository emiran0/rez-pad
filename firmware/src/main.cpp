// rez-pad firmware entry point.
// Phase 1: display bring-up (ILI9488 + backlight PWM), SPI benchmark and a wiring check,
// driven from a debug console on UART0 (the USB-UART bridge port, 115200 baud).
#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <string.h>

#include "debug/console.h"
#include "debug/wiring_check.h"
#include "display/backlight.h"
#include "display/display.h"
#include "pins.h"
#include "rezpad_protocol.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

namespace {

constexpr uint8_t kDefaultBrightness = 80;

bool protocolSelfTest() {
  static const uint8_t kCheck[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  return rp::proto::crc16(kCheck, sizeof(kCheck)) == 0x29B1 &&
         rp::proto::crc32(kCheck, sizeof(kCheck)) == 0xCBF43926u;
}

void printInfo(Print& out) {
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  uint32_t flashSize = 0;
  esp_flash_get_size(nullptr, &flashSize);

  out.printf("fw %s, protocol v%u, built %s %s\n", FW_VERSION, rp::proto::kVersion, __DATE__, __TIME__);
  out.printf("chip ESP32-S3 rev %u.%u, %u cores, %lu MHz\n", static_cast<unsigned>(chip.revision / 100),
             static_cast<unsigned>(chip.revision % 100), static_cast<unsigned>(chip.cores),
             static_cast<unsigned long>(getCpuFrequencyMhz()));
  out.printf("flash %lu MB\n", static_cast<unsigned long>(flashSize / (1024 * 1024)));
  if (psramFound()) {
    out.printf("PSRAM %lu KB total, %lu KB free\n", static_cast<unsigned long>(ESP.getPsramSize() / 1024),
               static_cast<unsigned long>(ESP.getFreePsram() / 1024));
  } else {
    out.println("PSRAM NOT FOUND (check board/memory_type settings)");
  }
  out.printf("internal heap %lu KB free\n", static_cast<unsigned long>(ESP.getFreeHeap() / 1024));
  out.printf("protocol self-test %s\n", protocolSelfTest() ? "ok" : "FAILED");
  out.printf("display %dx%d, SPI %.1f MHz, backlight %u%%\n", static_cast<int>(rp::display::lcd().width()),
             static_cast<int>(rp::display::lcd().height()), rp::display::effectiveSpiHz(rp::display::spiHz()) / 1e6,
             rp::backlight::get());
}

bool parseUint(const char* s, long lo, long hi, long& out) {
  char* end = nullptr;
  const long v = strtol(s, &end, 10);
  if (end == s || *end != '\0' || v < lo || v > hi) return false;
  out = v;
  return true;
}

// ---- console commands ----

void cmdInfo(int, char**, Print& out) { printInfo(out); }

void cmdI2c(int, char**, Print& out) { rp::wiring::scanI2c(out); }

void cmdPins(int, char**, Print& out) { rp::wiring::printInputLevels(out); }

void cmdBacklight(int argc, char** argv, Print& out) {
  long v = 0;
  if (argc == 2 && parseUint(argv[1], 0, 100, v)) rp::backlight::set(static_cast<uint8_t>(v));
  else if (argc != 1) out.println("usage: bl <0-100>");
  out.printf("backlight %u%%\n", rp::backlight::get());
}

void cmdSpi(int argc, char** argv, Print& out) {
  long mhz = 0;
  if (argc == 2 && parseUint(argv[1], 1, 80, mhz)) {
    rp::display::setSpiHz(static_cast<uint32_t>(mhz) * 1000000u);
    rp::display::drawTestPattern();
  } else if (argc != 1) {
    out.println("usage: spi <1-80>  (MHz, redraws the test pattern)");
  }
  out.printf("SPI requested %.1f MHz, effective %.1f MHz\n", rp::display::spiHz() / 1e6,
             rp::display::effectiveSpiHz(rp::display::spiHz()) / 1e6);
}

void cmdPattern(int, char**, Print& out) {
  const uint32_t t0 = micros();
  rp::display::drawTestPattern();
  out.printf("test pattern drawn in %.1f ms\n", (micros() - t0) / 1000.0f);
}

void cmdFill(int argc, char** argv, Print& out) {
  struct Named {
    const char* name;
    uint32_t color;
  };
  static const Named kColors[] = {{"black", TFT_BLACK}, {"white", TFT_WHITE}, {"red", TFT_RED},
                                  {"green", TFT_GREEN}, {"blue", TFT_BLUE},   {"grey", TFT_DARKGREY}};
  if (argc == 2) {
    for (const auto& c : kColors) {
      if (strcmp(argv[1], c.name) == 0) {
        rp::display::lcd().fillScreen(c.color);
        return;
      }
    }
  }
  out.println("usage: fill <black|white|red|green|blue|grey>");
}

void cmdRotation(int argc, char** argv, Print& out) {
  long r = 0;
  if (argc == 2 && parseUint(argv[1], 0, 3, r)) {
    rp::display::setRotation(static_cast<uint8_t>(r));
    rp::display::drawTestPattern();
  } else {
    out.println("usage: rot <0-3>  (1 and 3 are landscape)");
  }
}

void cmdInvert(int argc, char** argv, Print& out) {
  long v = 0;
  if (argc == 2 && parseUint(argv[1], 0, 1, v)) rp::display::setInvert(v != 0);
  else out.println("usage: invert <0|1>");
}

void cmdBench(int argc, char** argv, Print& out) {
  static const uint32_t kDefault[] = {20000000, 40000000, 60000000, 80000000};
  uint32_t list[6];
  size_t n = 0;
  for (int i = 1; i < argc && n < 6; ++i) {
    long mhz = 0;
    if (!parseUint(argv[i], 1, 80, mhz)) {
      out.println("usage: bench [MHz ...]  (default 20 40 60 80)");
      return;
    }
    list[n++] = static_cast<uint32_t>(mhz) * 1000000u;
  }
  if (n == 0) rp::display::runBenchmark(kDefault, 4, out);
  else rp::display::runBenchmark(list, n, out);
}

const rp::console::Command kCommands[] = {
    {"info", "", cmdInfo},
    {"i2c", "", cmdI2c},
    {"pins", "", cmdPins},
    {"bl", "<0-100>", cmdBacklight},
    {"spi", "<MHz>", cmdSpi},
    {"pattern", "", cmdPattern},
    {"fill", "<color>", cmdFill},
    {"rot", "<0-3>", cmdRotation},
    {"invert", "<0|1>", cmdInvert},
    {"bench", "[MHz ...]", cmdBench},
};

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("hello from rez-pad");

  rp::backlight::begin();  // off while the panel initializes, avoids a white flash
  const bool lcdOk = rp::display::begin();
  rp::display::drawTestPattern();
  rp::backlight::set(kDefaultBrightness);

  printInfo(Serial);
  if (!lcdOk) Serial.println("display init FAILED");
  rp::wiring::scanI2c(Serial);
  rp::wiring::printInputLevels(Serial);

  rp::console::begin(kCommands, sizeof(kCommands) / sizeof(kCommands[0]));
  Serial.println("ready, type help");
}

void loop() {
  rp::console::poll();
  delay(5);
}
