// Page manager: builds every page's widget grid once, shows one page at a time.
// Page switches are instant (hide/show), no slide animation (SPI bandwidth, see CLAUDE.md).
#pragma once

#include <stdint.h>

#include "widgets/widget.h"

namespace rp {
namespace ui {

struct PageSpec {
  const char* id;
  const char* name;
  const widgets::Spec* widgets;
  uint8_t count;
};

namespace pages {

constexpr int kMaxPages = 8;

// Builds the pages under `parent` (the grid area). Invalid widgets (unknown type, bad size,
// out of grid, overlapping) are replaced by a small error card and logged, never fatal.
void build(lv_obj_t* parent, const PageSpec* specs, int count, int cols, int rows);

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
