// Sensor logic tests. Run: pio test -e native
#include <unity.h>

#include "sensor_logic.h"

using namespace rp::sensor;

void setUp() {}
void tearDown() {}

static void test_aht20_crc_check_value() {
  // CRC-8 poly 0x31 init 0xFF (CRC-8/NRSC-5) check value for "123456789".
  const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX8(0xF7, aht20Crc8(check, sizeof(check)));
}

static uint8_t* makeFrame(uint8_t f[7], uint32_t rawH, uint32_t rawT, uint8_t status) {
  f[0] = status;
  f[1] = static_cast<uint8_t>(rawH >> 12);
  f[2] = static_cast<uint8_t>(rawH >> 4);
  f[3] = static_cast<uint8_t>(((rawH & 0x0F) << 4) | ((rawT >> 16) & 0x0F));
  f[4] = static_cast<uint8_t>(rawT >> 8);
  f[5] = static_cast<uint8_t>(rawT);
  f[6] = aht20Crc8(f, 6);
  return f;
}

static void test_aht20_parse() {
  uint8_t f[7];
  float t = 0, h = 0;
  // 50 % RH, 25 C: rawH = 0.5 * 2^20, rawT = (25 + 50) / 200 * 2^20
  makeFrame(f, 524288, 393216, 0x1C);
  TEST_ASSERT_EQUAL(Aht20Status::kOk, aht20Parse(f, t, h));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, h);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, t);
}

static void test_aht20_busy_and_crc() {
  uint8_t f[7];
  float t = 0, h = 0;
  makeFrame(f, 524288, 393216, 0x9C);
  TEST_ASSERT_EQUAL(Aht20Status::kBusy, aht20Parse(f, t, h));
  makeFrame(f, 524288, 393216, 0x1C);
  f[6] ^= 0x01;
  TEST_ASSERT_EQUAL(Aht20Status::kBadCrc, aht20Parse(f, t, h));
}

// Example values from the BMP280 datasheet (section 3.12).
static Bmp280Calib datasheetCalib() {
  Bmp280Calib c;
  c.t1 = 27504; c.t2 = 26435; c.t3 = -1000;
  c.p1 = 36477; c.p2 = -10685; c.p3 = 3024; c.p4 = 2855; c.p5 = 140;
  c.p6 = -7; c.p7 = 15500; c.p8 = -14600; c.p9 = 6000;
  return c;
}

static void test_bmp280_datasheet_example() {
  const Bmp280Calib c = datasheetCalib();
  int32_t tFine = 0;
  TEST_ASSERT_EQUAL_INT32(2508, bmp280CompensateT(519888, c, tFine));
  TEST_ASSERT_EQUAL_INT32(128422, tFine);
  const uint32_t p = bmp280CompensateP(415148, c, tFine);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 100653.27f, p / 256.0f);
}

static void test_bmp280_parse_calib_and_adc() {
  const uint8_t raw[24] = {0x70, 0x6B, 0x43, 0x67, 0x18, 0xFC, 0x7D, 0x8E, 0x43, 0xD6, 0xD0, 0x0B,
                           0x27, 0x0B, 0x8C, 0x00, 0xF9, 0xFF, 0x8C, 0x3C, 0xF8, 0xC6, 0x70, 0x17};
  const Bmp280Calib c = bmp280ParseCalib(raw);
  TEST_ASSERT_EQUAL_UINT16(27504, c.t1);
  TEST_ASSERT_EQUAL_INT16(26435, c.t2);
  TEST_ASSERT_EQUAL_INT16(-1000, c.t3);
  TEST_ASSERT_EQUAL_UINT16(36477, c.p1);
  TEST_ASSERT_EQUAL_INT16(-10685, c.p2);
  TEST_ASSERT_EQUAL_INT16(-7, c.p6);
  TEST_ASSERT_EQUAL_INT16(6000, c.p9);

  const uint8_t adc[6] = {0x65, 0x5A, 0xC0, 0x7E, 0xED, 0x00};
  int32_t p = 0, t = 0;
  bmp280ParseAdc(adc, p, t);
  TEST_ASSERT_EQUAL_INT32(415148, p);
  TEST_ASSERT_EQUAL_INT32(519888, t);
}

static void test_median_ema_rejects_spike() {
  MedianEma f(0.5f);
  f.update(20.0f);
  f.update(20.0f);
  f.update(20.0f);
  const float v = f.update(80.0f);  // single spike: median of (80, 20, 20) is 20
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, v);
  f.update(21.0f);
  TEST_ASSERT_TRUE(f.value() > 20.0f && f.value() < 21.0f);
}

static void test_humidity_correction() {
  // Sensor 2 C warmer than the room reads lower RH; correcting must raise it.
  const float rh = correctHumidity(45.0f, 24.0f, 22.0f);
  TEST_ASSERT_TRUE(rh > 50.0f && rh < 52.0f);  // about 50.9 %
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 45.0f, correctHumidity(45.0f, 22.0f, 22.0f));
  TEST_ASSERT_EQUAL_FLOAT(100.0f, correctHumidity(95.0f, 30.0f, 20.0f));
}

static void test_trend_unknown_until_one_hour() {
  PressureTrend tr;
  float d = 0;
  for (uint32_t s = 0; s < 3000; s += 5) tr.add(s, 1013.0f);
  TEST_ASSERT_EQUAL(Trend::kUnknown, tr.get(3000, 1.0f, d));
}

static void test_trend_rising_falling_steady() {
  PressureTrend rising, falling, steady;
  float d = 0;
  for (uint32_t s = 0; s <= 4 * 3600; s += 5) {
    const float hours = s / 3600.0f;
    rising.add(s, 1000.0f + 0.6f * hours);    // +1.8 hPa per 3 h
    falling.add(s, 1000.0f - 0.6f * hours);
    steady.add(s, 1000.0f + 0.1f * hours);    // +0.3 hPa per 3 h
  }
  const uint32_t now = 4 * 3600;
  TEST_ASSERT_EQUAL(Trend::kRising, rising.get(now, 1.0f, d));
  TEST_ASSERT_FLOAT_WITHIN(0.15f, 1.8f, d);
  TEST_ASSERT_EQUAL(Trend::kFalling, falling.get(now, 1.0f, d));
  TEST_ASSERT_FLOAT_WITHIN(0.15f, -1.8f, d);
  TEST_ASSERT_EQUAL(Trend::kSteady, steady.get(now, 1.0f, d));
  TEST_ASSERT_TRUE(steady.count() <= PressureTrend::kSlots);
}

static void test_trend_extrapolates_short_span() {
  PressureTrend tr;
  float d = 0;
  for (uint32_t s = 0; s <= 5400; s += 5) tr.add(s, 1000.0f + 0.5f * s / 3600.0f);  // 1.5 h at +0.5 hPa/h
  TEST_ASSERT_EQUAL(Trend::kRising, tr.get(5400, 1.0f, d));
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 1.5f, d);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_aht20_crc_check_value);
  RUN_TEST(test_aht20_parse);
  RUN_TEST(test_aht20_busy_and_crc);
  RUN_TEST(test_bmp280_datasheet_example);
  RUN_TEST(test_bmp280_parse_calib_and_adc);
  RUN_TEST(test_median_ema_rejects_spike);
  RUN_TEST(test_humidity_correction);
  RUN_TEST(test_trend_unknown_until_one_hour);
  RUN_TEST(test_trend_rising_falling_steady);
  RUN_TEST(test_trend_extrapolates_short_span);
  return UNITY_END();
}
