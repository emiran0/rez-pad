#include "sensor_logic.h"

#include <math.h>

namespace rp {
namespace sensor {

// ---- AHT20 ----

uint8_t aht20Crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

Aht20Status aht20Parse(const uint8_t frame[7], float& tempC, float& humPct) {
  if (frame[0] & 0x80) return Aht20Status::kBusy;
  if (aht20Crc8(frame, 6) != frame[6]) return Aht20Status::kBadCrc;
  const uint32_t rawH = (static_cast<uint32_t>(frame[1]) << 12) | (static_cast<uint32_t>(frame[2]) << 4) |
                        (frame[3] >> 4);
  const uint32_t rawT = (static_cast<uint32_t>(frame[3] & 0x0F) << 16) | (static_cast<uint32_t>(frame[4]) << 8) |
                        frame[5];
  humPct = rawH * 100.0f / 1048576.0f;
  tempC = rawT * 200.0f / 1048576.0f - 50.0f;
  return Aht20Status::kOk;
}

// ---- BMP280 ----

namespace {
uint16_t u16le(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
int16_t s16le(const uint8_t* p) { return static_cast<int16_t>(u16le(p)); }
}  // namespace

Bmp280Calib bmp280ParseCalib(const uint8_t raw[24]) {
  Bmp280Calib c;
  c.t1 = u16le(raw + 0);
  c.t2 = s16le(raw + 2);
  c.t3 = s16le(raw + 4);
  c.p1 = u16le(raw + 6);
  c.p2 = s16le(raw + 8);
  c.p3 = s16le(raw + 10);
  c.p4 = s16le(raw + 12);
  c.p5 = s16le(raw + 14);
  c.p6 = s16le(raw + 16);
  c.p7 = s16le(raw + 18);
  c.p8 = s16le(raw + 20);
  c.p9 = s16le(raw + 22);
  return c;
}

void bmp280ParseAdc(const uint8_t raw[6], int32_t& adcP, int32_t& adcT) {
  adcP = static_cast<int32_t>((static_cast<uint32_t>(raw[0]) << 12) | (static_cast<uint32_t>(raw[1]) << 4) |
                              (raw[2] >> 4));
  adcT = static_cast<int32_t>((static_cast<uint32_t>(raw[3]) << 12) | (static_cast<uint32_t>(raw[4]) << 4) |
                              (raw[5] >> 4));
}

int32_t bmp280CompensateT(int32_t adcT, const Bmp280Calib& c, int32_t& tFine) {
  const int32_t t1 = c.t1;
  // Datasheet formula; >> on negative values is an arithmetic shift on GCC (floor), as the datasheet assumes.
  const int32_t var1 = (((adcT >> 3) - (t1 * 2)) * static_cast<int32_t>(c.t2)) >> 11;
  const int32_t d = (adcT >> 4) - t1;
  const int32_t var2 = (((d * d) >> 12) * static_cast<int32_t>(c.t3)) >> 14;
  tFine = var1 + var2;
  return (tFine * 5 + 128) >> 8;
}

uint32_t bmp280CompensateP(int32_t adcP, const Bmp280Calib& c, int32_t tFine) {
  // Datasheet 64-bit integer formula, with shifts of signed values written as
  // multiplications/divisions by powers of two (right shifts kept where the datasheet relies
  // on arithmetic shift of possibly negative values, which GCC and Xtensa implement).
  int64_t var1 = static_cast<int64_t>(tFine) - 128000;
  int64_t var2 = var1 * var1 * static_cast<int64_t>(c.p6);
  var2 = var2 + ((var1 * static_cast<int64_t>(c.p5)) * 131072);
  var2 = var2 + (static_cast<int64_t>(c.p4) * 34359738368LL);
  var1 = ((var1 * var1 * static_cast<int64_t>(c.p3)) >> 8) + ((var1 * static_cast<int64_t>(c.p2)) * 4096);
  var1 = ((static_cast<int64_t>(1) << 47) + var1) * static_cast<int64_t>(c.p1) >> 33;
  if (var1 == 0) return 0;
  int64_t p = 1048576 - adcP;
  p = (((p * 2147483648LL) - var2) * 3125) / var1;
  var1 = (static_cast<int64_t>(c.p9) * (p >> 13) * (p >> 13)) >> 25;
  var2 = (static_cast<int64_t>(c.p8) * p) >> 19;
  p = ((p + var1 + var2) >> 8) + (static_cast<int64_t>(c.p7) * 16);
  return p > 0 ? static_cast<uint32_t>(p) : 0;
}

// ---- Filtering ----

float MedianEma::update(float x) {
  hist_[2] = hist_[1];
  hist_[1] = hist_[0];
  hist_[0] = x;
  if (n_ < 3) ++n_;

  float m = x;
  if (n_ >= 3) {
    const float a = hist_[0], b = hist_[1], c = hist_[2];
    m = (a > b) ? ((b > c) ? b : (a > c ? c : a)) : ((a > c) ? a : (b > c ? c : b));
  }
  ema_ = (n_ == 1) ? m : ema_ + alpha_ * (m - ema_);
  return ema_;
}

float correctHumidity(float rhPct, float tMeasuredC, float tActualC) {
  auto es = [](float t) { return 6.112f * expf(17.62f * t / (243.12f + t)); };
  float rh = rhPct * es(tMeasuredC) / es(tActualC);
  if (rh < 0) rh = 0;
  if (rh > 100) rh = 100;
  return rh;
}

// ---- Pressure trend ----

const char* trendName(Trend t) {
  switch (t) {
    case Trend::kFalling: return "falling";
    case Trend::kSteady: return "steady";
    case Trend::kRising: return "rising";
    default: return "unknown";
  }
}

void PressureTrend::add(uint32_t nowS, float hPa) {
  if (count_ > 0 && nowS - slots_[head_].t < kBucketS) {
    slots_[head_].p = hPa;  // same bucket: keep its start time, store the latest value
    return;
  }
  if (count_ > 0) head_ = (head_ + 1) % kSlots;
  slots_[head_] = {nowS, hPa};
  if (count_ < kSlots) ++count_;
}

Trend PressureTrend::get(uint32_t nowS, float thresholdHpa, float& delta3h) const {
  delta3h = 0;
  if (count_ < 2) return Trend::kUnknown;
  const Sample& newest = slots_[head_];
  // Walk from the oldest stored sample to the first one inside the window.
  const Sample* oldest = nullptr;
  for (size_t i = count_; i-- > 0;) {
    const Sample& s = slots_[(head_ + kSlots - i) % kSlots];
    if (nowS - s.t <= kWindowS) {
      oldest = &s;
      break;
    }
  }
  if (!oldest) return Trend::kUnknown;
  const uint32_t span = newest.t - oldest->t;
  if (span < kMinSpanS) return Trend::kUnknown;
  delta3h = (newest.p - oldest->p) * static_cast<float>(kWindowS) / static_cast<float>(span);
  if (delta3h > thresholdHpa) return Trend::kRising;
  if (delta3h < -thresholdHpa) return Trend::kFalling;
  return Trend::kSteady;
}

}  // namespace sensor
}  // namespace rp
