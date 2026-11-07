// Page manager: builds every page's widget grid once, shows one page at a time.
// Page switches are instant (hide/show), no slide animation (SPI bandwidth, see CLAUDE.md).
#pragma once

#include <stdint.h>

#include "config_logic.h"
#include "widgets/widget.h"

namespace rp {
namespace ui {
namespace pages {

constexpr int kMaxPages = cfg::kMaxPages;

// Builds the config's pages under `parent` (the grid area). The config is already validated; a
// widget the firmware cannot create gets a small error card instead, never a crash. `cfg` must
// stay alive until clear() (widgets keep references to their specs).
void build(lv_obj_t* parent, const cfg::Config& cfg);

// Deletes all pages and widgets (before building a new config).
void clear();

int count();
int current();
const char* name(int index);
void show(int index);
void step(int delta);  // wraps around

// Refreshes widgets on all pages whose interests match `changed`.
void refresh(const Model& m, uint32_t changed, uint32_t nowMs);

}  // namespace pages
}  // namespace ui
}  // namespace rp
