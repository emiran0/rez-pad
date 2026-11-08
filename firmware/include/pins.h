// rez-pad pin map. Single source of truth for GPIO assignments.
// Mirrored in docs/hardware.md: change both or neither.
//
// Status: proposed, NOT yet verified against the physical board and modules.
//
// Reserved, never assign:
//   GPIO26..37  flash and octal PSRAM
//   GPIO19, 20  native USB D-/D+
//   GPIO43, 44  UART0 (USB-UART bridge, debug logs)
//   GPIO0, 3, 45, 46  strapping pins
//   GPIO38, 48  onboard RGB LED (38 on DevKitC-1 v1.1, 48 on v1.0 and most clones), unconfirmed
#pragma once

#include <stdint.h>

namespace pins {

// Display: ILI9488 on SPI2 (FSPI) IOMUX pins. MISO/SDO and all touch pins are NOT connected.
constexpr int8_t kTftSck = 12;
constexpr int8_t kTftMosi = 11;
constexpr int8_t kTftCs = 10;
constexpr int8_t kTftDc = 9;
constexpr int8_t kTftRst = 14;
constexpr int8_t kTftBacklight = 21;  // LEDC PWM, active high. Module switches it via its own transistor (Q1).
constexpr int8_t kTftMiso = -1;

// SD card (optional, later): separate SPI3 bus.
constexpr int8_t kSdSck = 39;
constexpr int8_t kSdMosi = 40;
constexpr int8_t kSdMiso = 41;
constexpr int8_t kSdCs = 42;

// I2C: AHT20 (0x38) + BMP280 (0x76 or 0x77), 400 kHz.
constexpr int8_t kI2cSda = 1;
constexpr int8_t kI2cScl = 2;

// Encoders (KY-040, powered from 3.3 V only).
constexpr int8_t kEnc1A = 4;   // CLK, PCNT unit 0
constexpr int8_t kEnc1B = 5;   // DT
constexpr int8_t kEnc1Sw = 6;  // internal pull-up
constexpr int8_t kEnc2A = 7;   // CLK, PCNT unit 1
constexpr int8_t kEnc2B = 15;  // DT
constexpr int8_t kEnc2Sw = 16; // internal pull-up

// Optional buttons (0 to 4 fitted, config inputs.buttons): tact switch to GND, internal pull-up.
constexpr int8_t kBtn1 = 17;
constexpr int8_t kBtn2 = 18;
constexpr int8_t kBtn3 = 8;
constexpr int8_t kBtn4 = 13;

// Spare: 47.

}  // namespace pins
