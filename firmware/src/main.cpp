// rez-pad firmware entry point.
// Phase 0: boot banner on UART0 (bridge USB-C port) with chip, flash and PSRAM info.
#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_flash.h>

#include "rezpad_protocol.h"
#include "pins.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

namespace {

bool protocolSelfTest() {
  static const uint8_t kCheck[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  return rp::proto::crc16(kCheck, sizeof(kCheck)) == 0x29B1 &&
         rp::proto::crc32(kCheck, sizeof(kCheck)) == 0xCBF43926u;
}

void printBanner() {
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  uint32_t flashSize = 0;
  esp_flash_get_size(nullptr, &flashSize);

  Serial.println();
  Serial.println("hello from rez-pad");
  Serial.printf("fw %s, protocol v%u, built %s %s\n", FW_VERSION, rp::proto::kVersion, __DATE__, __TIME__);
  Serial.printf("chip ESP32-S3 rev %u.%u, %u cores, %lu MHz\n", static_cast<unsigned>(chip.revision / 100),
                static_cast<unsigned>(chip.revision % 100), static_cast<unsigned>(chip.cores),
                static_cast<unsigned long>(getCpuFrequencyMhz()));
  Serial.printf("flash %lu MB\n", static_cast<unsigned long>(flashSize / (1024 * 1024)));
  if (psramFound()) {
    Serial.printf("PSRAM %lu KB total, %lu KB free\n", static_cast<unsigned long>(ESP.getPsramSize() / 1024),
                  static_cast<unsigned long>(ESP.getFreePsram() / 1024));
  } else {
    Serial.println("PSRAM NOT FOUND (check board/memory_type settings)");
  }
  Serial.printf("internal heap %lu KB free\n", static_cast<unsigned long>(ESP.getFreeHeap() / 1024));
  Serial.printf("protocol self-test %s\n", protocolSelfTest() ? "ok" : "FAILED");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  printBanner();
}

void loop() {
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (now - last >= 5000) {
    last = now;
    Serial.printf("alive %lus, heap %lu KB, psram %lu KB\n", static_cast<unsigned long>(now / 1000),
                  static_cast<unsigned long>(ESP.getFreeHeap() / 1024),
                  static_cast<unsigned long>(ESP.getFreePsram() / 1024));
  }
  delay(10);
}
