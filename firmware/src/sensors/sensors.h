// AHT20 (temperature, humidity) + BMP280 (pressure) on the shared I2C bus.
// A background task reads both every 5 s, filters, applies offsets and tracks the pressure trend.
#pragma once

#include <stdint.h>

#include "sensor_logic.h"

namespace rp {
namespace sensors {

struct Config {
  float tempOffsetC = -1.5f;  // added to the AHT20 temperature (self-heating correction)
  float humOffsetPct = 0.0f;  // added after the temperature-based humidity correction
  float trendThresholdHpa = 1.0f;
};

struct Reading {
  bool ahtOk = false;
  bool bmpOk = false;
  uint8_t bmpAddr = 0;      // 0x76 or 0x77, 0 if not found

  // Values for display: filtered, offsets applied.
  float tempC = 0;          // room temperature (AHT20)
  float humPct = 0;         // corrected for the temperature offset, plus humOffsetPct
  float pressHpa = 0;
  sensor::Trend trend = sensor::Trend::kUnknown;
  float trendDelta3h = 0;   // hPa per 3 h

  // Diagnostics: last unfiltered readings.
  float rawTempC = 0;
  float rawHumPct = 0;
  float rawPressHpa = 0;
  float bmpTempC = 0;

  uint32_t samples = 0;
  uint32_t ahtErrors = 0;
  uint32_t bmpErrors = 0;
  uint32_t updatedMs = 0;   // millis() of the last successful read, 0 if none yet
};

bool begin(const Config& cfg);
void setConfig(const Config& cfg);
Config config();

Reading latest();

}  // namespace sensors
}  // namespace rp
