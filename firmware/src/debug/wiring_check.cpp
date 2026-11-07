#include "wiring_check.h"

#include <Wire.h>

#include "pins.h"

namespace rp {
namespace wiring {

namespace {

const char* knownDevice(uint8_t addr) {
  switch (addr) {
    case 0x38: return "AHT20 (temp/humidity)";
    case 0x76:
    case 0x77: return "BMP280 (pressure)";
    default: return "unknown";
  }
}

bool gWireStarted = false;

}  // namespace

int scanI2c(Print& out) {
  if (!gWireStarted) {
    Wire.begin(pins::kI2cSda, pins::kI2cScl, 400000);
    gWireStarted = true;
  }
  out.printf("I2C scan (SDA=%d, SCL=%d, 400 kHz):\n", pins::kI2cSda, pins::kI2cScl);
  int found = 0;
  bool aht = false;
  bool bmp = false;
  for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      out.printf("  0x%02X  %s\n", addr, knownDevice(addr));
      ++found;
      aht = aht || addr == 0x38;
      bmp = bmp || addr == 0x76 || addr == 0x77;
    }
  }
  if (found == 0) out.println("  no devices: check VCC=3V3, GND, SDA/SCL not swapped");
  out.printf("  AHT20 %s, BMP280 %s\n", aht ? "found" : "MISSING", bmp ? "found" : "MISSING");
  return found;
}

void printInputLevels(Print& out) {
  struct Input {
    const char* name;
    int8_t pin;
  };
  static const Input kInputs[] = {
      {"enc1 CLK", pins::kEnc1A}, {"enc1 DT", pins::kEnc1B}, {"enc1 SW", pins::kEnc1Sw},
      {"enc2 CLK", pins::kEnc2A}, {"enc2 DT", pins::kEnc2B}, {"enc2 SW", pins::kEnc2Sw},
      {"btn1", pins::kBtn1},      {"btn2", pins::kBtn2},
  };
  out.println("input levels (pull-up on, idle should be HIGH):");
  for (const auto& in : kInputs) {
    pinMode(in.pin, INPUT_PULLUP);
  }
  delay(2);
  for (const auto& in : kInputs) {
    out.printf("  %-9s GPIO%-2d %s\n", in.name, in.pin, digitalRead(in.pin) ? "HIGH" : "LOW");
  }
  out.println("  (unconnected pins also read HIGH because of the pull-up)");
}

}  // namespace wiring
}  // namespace rp
