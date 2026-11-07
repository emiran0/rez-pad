// Widget grid building blocks. A page is a grid of cards; each card hosts one widget.
// Widget types and allowed sizes follow docs/config-schema.md.
#pragma once

#include <lvgl.h>
#include <math.h>
#include <stdint.h>

#include "ui/model.h"

namespace rp {
namespace widgets {

// One widget on a page (mirrors a "widgets" entry in the config JSON).
struct Spec {
  const char* type;
  uint8_t x, y, w, h;          // grid cell position and size
  const char* source = nullptr;  // data source id for metric_* widgets
  const char* label = nullptr;
  const char* unit = nullptr;
  float min = 0, max = 100;
  float warn = NAN, crit = NAN;  // thresholds for color changes (NAN = not set)
  int8_t decimals = -1;          // -1 = automatic
  const char* mode = nullptr;    // weather_forecast: "daily" or "hourly"
};

class Widget {
 public:
  explicit Widget(const Spec& spec) : spec_(spec) {}
  virtual ~Widget() = default;

  // Creates the LVGL children inside `card`; (w, h) is the card's content area in px.
  virtual void build(lv_obj_t* card, int w, int h) = 0;

  // Which ui::Changed bits this widget cares about.
  virtual uint32_t interests() const = 0;

  // Updates the LVGL objects from the model. Only called when an interesting bit changed.
  virtual void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) = 0;

  const Spec& spec() const { return spec_; }

 protected:
  Spec spec_;
};

// Returns nullptr for an unknown type.
Widget* create(const Spec& spec);

// Allowed sizes per type (docs/config-schema.md). Unknown types return false.
bool sizeAllowed(const char* type, int w, int h);

}  // namespace widgets
}  // namespace rp
