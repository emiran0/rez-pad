// 20 px status bar: clock, page name, up to 5 page dots (a sliding window when there are more
// pages), up to 3 status items from the config (date, room, outdoor, volume, any data source), and
// the host icon.
#pragma once

#include <lvgl.h>

#include "config_logic.h"
#include "model.h"

namespace rp {
namespace ui {
namespace status_bar {

void build(lv_obj_t* parent);
void setPage(int index, int count, const char* name);
// The config's status_bar items (call after every config).
void setItems(const cfg::Config& c);
void refresh(const Model& m, uint32_t changed);
// Small warning next to the host icon ("" hides it), e.g. the stored config was invalid.
void setWarning(const char* text);
// A hint in place of the clock, page name and dots ("" or nullptr restores them).
void setHint(const char* text);

}  // namespace status_bar
}  // namespace ui
}  // namespace rp
