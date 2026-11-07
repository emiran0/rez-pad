// Line-based debug console on UART0 (the USB-UART bridge port, 115200 baud).
// Development and measurement only; the host talks to the device over the native USB protocol.
#pragma once

#include <Arduino.h>

namespace rp {
namespace console {

using Handler = void (*)(int argc, char** argv, Print& out);

struct Command {
  const char* name;
  const char* usage;
  Handler fn;
};

// Registers the command table (must stay alive for the program lifetime).
void begin(const Command* commands, size_t count);

// Reads pending characters and runs complete lines. Call from loop().
void poll();

}  // namespace console
}  // namespace rp
