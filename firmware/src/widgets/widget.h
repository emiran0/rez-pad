// Widget grid building blocks. A page is a grid of cards; each card hosts one widget.
// Widget types and allowed sizes follow docs/config-schema.md.
#pragma once

#include <lvgl.h>
#include <math.h>
#include <stdint.h>

#include "config_logic.h"
#include "ui/model.h"

namespace rp {
namespace widgets {

// One widget on a page: a "widgets" entry of the config, parsed and validated by lib/config_logic
// (strings are empty when not set; unit, range and thresholds already have source defaults).
using Spec = cfg::WidgetSpec;

class Widget {
 public:
  // `spec` lives in the active config, which outlives the widget (pages are rebuilt first).
  explicit Widget(const Spec& spec) : spec_(spec) {}
  virtual ~Widget() = default;

  // Creates the LVGL children inside `card`; (w, h) is the card's content area in px.
  virtual void build(lv_obj_t* card, int w, int h) = 0;

  // True for widgets drawn edge to edge (no card padding), e.g. images.
  virtual bool fullBleed() const { return false; }

  // Which ui::Changed bits this widget cares about.
  virtual uint32_t interests() const = 0;

  // Updates the LVGL objects from the model. Only called when an interesting bit changed.
  virtual void refresh(const ui::Model& m, uint32_t changed, uint32_t nowMs) = 0;

  const Spec& spec() const { return spec_; }

 protected:
  const Spec& spec_;
};

// Returns nullptr for an unknown type.
Widget* create(const Spec& spec);


}  // namespace widgets
}  // namespace rp
