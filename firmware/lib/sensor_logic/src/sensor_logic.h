// Hardware-independent sensor logic: AHT20 frame parsing, BMP280 compensation,
// filtering, humidity self-heating correction and pressure trend.
// Pure C++, unit-tested on the host (test/test_sensor).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace rp {
namespace sensor {

// ---- AHT20 ----

// CRC-8, poly 0x31, init 0xFF (AHT20 datasheet).
uint8_t aht20Crc8(const uint8_t* data, size_t len);

enum class Aht20Status : uint8_t { kOk, kBusy, kBadCrc };

// Parses the 7-byte measurement frame [status, 5 data bytes, crc].
Aht20Status aht20Parse(const uint8_t frame[7], float& tempC, float& humPct);

// ---- BMP280 ----

struct Bmp280Calib {
  uint16_t t1;
  int16_t t2, t3;
  uint16_t p1;
  int16_t p2, p3, p4, p5, p6, p7, p8, p9;
};

// From the 24 calibration bytes starting at register 0x88.
Bmp280Calib bmp280ParseCalib(const uint8_t raw[24]);

// 20-bit ADC values from registers 0xF7..0xFC.
void bmp280ParseAdc(const uint8_t raw[6], int32_t& adcP, int32_t& adcT);

// Datasheet integer compensation. Temperature in 0.01 C; sets tFine for the pressure step.
int32_t bmp280CompensateT(int32_t adcT, const Bmp280Calib& c, int32_t& tFine);
// Pressure in Pa as unsigned Q24.8 (divide by 256). Returns 0 on invalid input.
uint32_t bmp280CompensateP(int32_t adcP, const Bmp280Calib& c, int32_t tFine);

// ---- Filtering ----

// Median of the last 3 samples (rejects single spikes), then an exponential moving average.
class MedianEma {
 public:
  explicit MedianEma(float alpha = 0.3f) : alpha_(alpha) {}
  float update(float x);
  bool ready() const { return n_ > 0; }
  float value() const { return ema_; }
  void reset() { n_ = 0; }

 private:
  float alpha_;
  float hist_[3] = {0, 0, 0};
  int n_ = 0;
  float ema_ = 0;
};

// The sensor reads warmer than the room because of nearby heat (ESP32, backlight), so its
// relative humidity reads too low. Converts RH measured at tMeasured to RH at tActual
// (Magnus formula, keeps the absolute water content). Clamped to 0..100.
float correctHumidity(float rhPct, float tMeasuredC, float tActualC);

// ---- Pressure trend ----

enum class Trend : uint8_t { kUnknown, kFalling, kSteady, kRising };
const char* trendName(Trend t);

// Keeps one pressure sample per bucket (default 5 min) for the last 3 hours.
class PressureTrend {
 public:
  static constexpr uint32_t kWindowS = 3 * 3600;
  static constexpr uint32_t kBucketS = 300;
  static constexpr uint32_t kMinSpanS = 3600;  // need 1 h of history before reporting
  static constexpr size_t kSlots = kWindowS / kBucketS + 1;

  void add(uint32_t nowS, float hPa);

  // Change over 3 h (extrapolated from the available span if shorter than 3 h).
  Trend get(uint32_t nowS, float thresholdHpa, float& delta3h) const;

  size_t count() const { return count_; }

 private:
  struct Sample {
    uint32_t t;
    float p;
  };
  Sample slots_[kSlots] = {};
  size_t head_ = 0;  // index of the newest sample
  size_t count_ = 0;
};

}  // namespace sensor
}  // namespace rp
