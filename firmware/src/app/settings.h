// Applies the non-UI parts of a config: encoder and button setup, sensor offsets, backlight
// brightness and SPI clock. Used at boot and whenever the host pushes a new config.
#pragma once

#include "config_logic.h"
#include "input/input.h"
#include "sensors/sensors.h"

namespace rp {
namespace settings {

input::Config inputConfig(const cfg::Config& c);
sensors::Config sensorConfig(const cfg::Config& c);

// Input, sensors, brightness and SPI clock (call from the UI task: the SPI clock change touches
// the display bus).
void apply(const cfg::Config& c);

}  // namespace settings
}  // namespace rp
