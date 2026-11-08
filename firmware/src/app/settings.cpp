#include "settings.h"

#include "actions.h"
#include "display/display.h"
#include "display/screen.h"

namespace rp {
namespace settings {

input::Config inputConfig(const cfg::Config& c) {
  input::Config in;
  for (int i = 0; i < 2; ++i) {
    in.enc[i].countsPerDetent = c.countsPerDetent[i];
    in.enc[i].invert = c.invert[i];
    // With no double-click mapped on any page, clicks fire on release instead of waiting for a
    // possible second click.
    bool dbl = !c.inputs.enc[i].dbl.none();
    for (int p = 0; p < c.pageCount; ++p) dbl = dbl || !c.pages[p].inputs.enc[i].dbl.none();
    in.enc[i].doubleEnabled = dbl;
  }
  for (int i = 0; i < actions::kMaxButtons && i < input::kMaxButtons; ++i) {
    bool dbl = !c.inputs.btn[i].dbl.none();
    for (int p = 0; p < c.pageCount; ++p) dbl = dbl || !c.pages[p].inputs.btn[i].dbl.none();
    in.btnDoubleEnabled[i] = dbl;
  }
  in.buttons = c.buttons;
  return in;
}

sensors::Config sensorConfig(const cfg::Config& c) {
  sensors::Config s;
  s.tempOffsetC = c.tempOffsetC;
  s.humOffsetPct = c.humOffsetPct;
  s.trendThresholdHpa = c.trendThresholdHpa;
  return s;
}

void apply(const cfg::Config& c) {
  input::setConfig(inputConfig(c));
  sensors::setConfig(sensorConfig(c));
  screen::apply(c);
  if (display::spiHz() != c.spiHz) display::setSpiHz(c.spiHz);
}

}  // namespace settings
}  // namespace rp
