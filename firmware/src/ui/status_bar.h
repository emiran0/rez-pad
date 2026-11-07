// 20 px status bar: clock, page name and dots, demo/host indicator.
#pragma once

#include <lvgl.h>

#include "model.h"

namespace rp {
namespace ui {
namespace status_bar {

void build(lv_obj_t* parent);
void setPage(int index, int count, const char* name);
void refresh(const Model& m, uint32_t changed);

}  // namespace status_bar
}  // namespace ui
}  // namespace rp
