#include "sensors.h"

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "i2c_bus.h"

namespace rp {
namespace sensors {

using sensor::Aht20Status;
using sensor::Bmp280Calib;

namespace {

constexpr uint8_t kAhtAddr = 0x38;
constexpr uint32_t kPeriodMs = 5000;

Config gCfg;
Reading gReading;
portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED;

// ---- small Wire helpers (callers hold the I2C lock) ----

bool writeBytes(uint8_t addr, const uint8_t* data, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

bool readBytes(uint8_t addr, uint8_t* out, size_t len) {
  if (Wire.requestFrom(addr, static_cast<uint8_t>(len)) != len) return false;
  for (size_t i = 0; i < len; ++i) out[i] = static_cast<uint8_t>(Wire.read());
  return true;
}

bool readRegs(uint8_t addr, uint8_t reg, uint8_t* out, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  return readBytes(addr, out, len);
}

bool writeReg(uint8_t addr, uint8_t reg, uint8_t value) {
  const uint8_t b[2] = {reg, value};
  return writeBytes(addr, b, 2);
}

// ---- AHT20 ----

bool ahtInit() {
  I2cLock lock;
  uint8_t status = 0;
  if (!readBytes(kAhtAddr, &status, 1)) return false;
  if (!(status & 0x08)) {  // not calibrated: send the init command
    const uint8_t init[3] = {0xBE, 0x08, 0x00};
    if (!writeBytes(kAhtAddr, init, 3)) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return true;
}

bool ahtRead(float& tempC, float& humPct) {
  {
    I2cLock lock;
    const uint8_t trigger[3] = {0xAC, 0x33, 0x00};
    if (!writeBytes(kAhtAddr, trigger, 3)) return false;
  }
  vTaskDelay(pdMS_TO_TICKS(80));  // datasheet: measurement takes about 75 ms
  for (int attempt = 0; attempt < 3; ++attempt) {
    uint8_t frame[7];
    bool got;
    {
      I2cLock lock;
      got = readBytes(kAhtAddr, frame, 7);
    }
    if (!got) return false;
    const Aht20Status st = sensor::aht20Parse(frame, tempC, humPct);
    if (st == Aht20Status::kOk) return true;
    if (st == Aht20Status::kBadCrc) return false;
    vTaskDelay(pdMS_TO_TICKS(20));  // busy, wait a bit more
  }
  return false;
}

// ---- BMP280 ----

uint8_t gBmpAddr = 0;
Bmp280Calib gCalib;

bool bmpInit() {
  I2cLock lock;
  for (uint8_t addr : {static_cast<uint8_t>(0x76), static_cast<uint8_t>(0x77)}) {
    // Address-only probe first: a register read to an absent device logs a driver error.
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;
    uint8_t id = 0;
    if (!readRegs(addr, 0xD0, &id, 1)) continue;
    // 0x58 BMP280 (0x56/0x57 engineering samples), 0x60 BME280 (pressure part is compatible).
    if (id != 0x58 && id != 0x56 && id != 0x57 && id != 0x60) continue;
    writeReg(addr, 0xE0, 0xB6);  // soft reset
    delay(5);
    uint8_t raw[24];
    if (!readRegs(addr, 0x88, raw, sizeof(raw))) return false;
    gCalib = sensor::bmp280ParseCalib(raw);
    writeReg(addr, 0xF5, 0x00);  // no IIR filter (we filter ourselves), no standby
    gBmpAddr = addr;
    return true;
  }
  return false;
}

bool bmpRead(float& pressHpa, float& tempC) {
  if (!gBmpAddr) return false;
  {
    I2cLock lock;
    // osrs_t x2, osrs_p x16, forced mode: one measurement then sleep (no self-heating between reads).
    if (!writeReg(gBmpAddr, 0xF4, 0x55)) return false;
  }
  vTaskDelay(pdMS_TO_TICKS(45));  // max measurement time for these settings is about 43 ms
  uint8_t raw[6];
  for (int attempt = 0; attempt < 5; ++attempt) {
    I2cLock lock;
    uint8_t status = 0;
    if (!readRegs(gBmpAddr, 0xF3, &status, 1)) return false;
    if (!(status & 0x08)) {
      if (!readRegs(gBmpAddr, 0xF7, raw, sizeof(raw))) return false;
      int32_t adcP = 0, adcT = 0;
      sensor::bmp280ParseAdc(raw, adcP, adcT);
      int32_t tFine = 0;
      tempC = sensor::bmp280CompensateT(adcT, gCalib, tFine) / 100.0f;
      const uint32_t p = sensor::bmp280CompensateP(adcP, gCalib, tFine);
      if (p == 0) return false;
      pressHpa = p / 256.0f / 100.0f;
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return false;
}

// ---- task ----

void task(void*) {
  sensor::MedianEma fTemp(0.3f), fHum(0.3f), fPress(0.3f);
  sensor::PressureTrend trend;
  bool ahtReady = ahtInit();
  bool bmpReady = bmpInit();
  uint32_t ahtErr = 0, bmpErr = 0, samples = 0;

  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    if (!ahtReady) ahtReady = ahtInit();
    if (!bmpReady) bmpReady = bmpInit();

    float t = 0, h = 0, p = 0, bt = 0;
    const bool ahtOk = ahtReady && ahtRead(t, h);
    const bool bmpOk = bmpReady && bmpRead(p, bt);
    if (ahtReady && !ahtOk) {
      ++ahtErr;
      ahtReady = false;  // re-init on the next cycle
    }
    if (bmpReady && !bmpOk) {
      ++bmpErr;
      bmpReady = false;
    }

    Config cfg;
    taskENTER_CRITICAL(&gLock);
    cfg = gCfg;
    taskEXIT_CRITICAL(&gLock);

    Reading r;
    taskENTER_CRITICAL(&gLock);
    r = gReading;
    taskEXIT_CRITICAL(&gLock);

    const uint32_t nowMs = millis();
    if (ahtOk) {
      const float ft = fTemp.update(t);
      const float fh = fHum.update(h);
      r.rawTempC = t;
      r.rawHumPct = h;
      r.tempC = ft + cfg.tempOffsetC;
      float hum = sensor::correctHumidity(fh, ft, r.tempC) + cfg.humOffsetPct;
      r.humPct = hum < 0 ? 0 : (hum > 100 ? 100 : hum);
    }
    if (bmpOk) {
      r.rawPressHpa = p;
      r.bmpTempC = bt;
      r.pressHpa = fPress.update(p);
      trend.add(nowMs / 1000, r.pressHpa);
      r.trend = trend.get(nowMs / 1000, cfg.trendThresholdHpa, r.trendDelta3h);
    }
    if (ahtOk || bmpOk) {
      ++samples;
      r.updatedMs = nowMs;
    }
    r.ahtOk = ahtOk;
    r.bmpOk = bmpOk;
    r.bmpAddr = gBmpAddr;
    r.samples = samples;
    r.ahtErrors = ahtErr;
    r.bmpErrors = bmpErr;

    taskENTER_CRITICAL(&gLock);
    gReading = r;
    taskEXIT_CRITICAL(&gLock);

    vTaskDelayUntil(&wake, pdMS_TO_TICKS(kPeriodMs));
  }
}

}  // namespace

bool begin(const Config& cfg) {
  gCfg = cfg;
  return xTaskCreatePinnedToCore(task, "sensors", 4096, nullptr, 2, nullptr, 0) == pdPASS;
}

void setConfig(const Config& cfg) {
  taskENTER_CRITICAL(&gLock);
  gCfg = cfg;
  taskEXIT_CRITICAL(&gLock);
}

Config config() {
  taskENTER_CRITICAL(&gLock);
  const Config c = gCfg;
  taskEXIT_CRITICAL(&gLock);
  return c;
}

Reading latest() {
  taskENTER_CRITICAL(&gLock);
  const Reading r = gReading;
  taskEXIT_CRITICAL(&gLock);
  return r;
}

}  // namespace sensors
}  // namespace rp
