// Built-in default layout, mirroring "pages" in the default config (docs/config-schema.md).
// Phase 9 replaces this with pages parsed from /config.json, falling back to this layout.
#pragma once

#include "pages.h"

namespace rp {
namespace ui {

const PageSpec* defaultPages(int& count);

}  // namespace ui
}  // namespace rp
