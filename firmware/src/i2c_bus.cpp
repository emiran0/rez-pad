#include "i2c_bus.h"

#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "pins.h"

namespace rp {

namespace {
SemaphoreHandle_t gMutex = nullptr;
bool gStarted = false;
portMUX_TYPE gInitLock = portMUX_INITIALIZER_UNLOCKED;
}  // namespace

void i2cBegin() {
  taskENTER_CRITICAL(&gInitLock);
  if (!gMutex) gMutex = xSemaphoreCreateRecursiveMutex();
  taskEXIT_CRITICAL(&gInitLock);
  xSemaphoreTakeRecursive(gMutex, portMAX_DELAY);
  if (!gStarted) {
    Wire.begin(pins::kI2cSda, pins::kI2cScl, 400000);
    Wire.setTimeOut(50);
    gStarted = true;
  }
  xSemaphoreGiveRecursive(gMutex);
}

I2cLock::I2cLock() {
  i2cBegin();
  xSemaphoreTakeRecursive(gMutex, portMAX_DELAY);
}

I2cLock::~I2cLock() { xSemaphoreGiveRecursive(gMutex); }

}  // namespace rp
