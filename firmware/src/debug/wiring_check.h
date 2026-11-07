// Quick checks that the breadboard wiring matches pins.h.
#pragma once

#include <Arduino.h>

namespace rp {
namespace wiring {

// Scans the sensor I2C bus and names the expected devices (AHT20 0x38, BMP280 0x76/0x77).
// Returns the number of devices found.
int scanI2c(Print& out);

// Prints the idle level of every encoder and button input (pull-ups enabled).
// KY-040 CLK/DT/SW idle HIGH; a stuck LOW usually means a wrong pin or a short to GND.
void printInputLevels(Print& out);

}  // namespace wiring
}  // namespace rp
