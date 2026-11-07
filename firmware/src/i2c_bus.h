// Shared sensor I2C bus (Wire on pins::kI2cSda / kI2cScl). The sensor task and the debug
// console both use it, so every transaction runs under this lock.
#pragma once

namespace rp {

// Starts Wire at 400 kHz. Safe to call more than once.
void i2cBegin();

// RAII lock around Wire use; also calls i2cBegin() on first use.
class I2cLock {
 public:
  I2cLock();
  ~I2cLock();
  I2cLock(const I2cLock&) = delete;
  I2cLock& operator=(const I2cLock&) = delete;
};

}  // namespace rp
