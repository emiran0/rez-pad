// Image asset files for `image` widgets (docs/protocol.md, "Assets"), stored in LittleFS under
// /a/. The link task saves what the host sends; image widgets load their file when they are
// built; after a config is applied, files no widget uses are deleted.
#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "config_logic.h"

namespace rp {
namespace assets {

// Name and size checks for a file the host wants to store (safe characters, ".rpa").
bool validName(const char* name);

// Writes /a/<name> atomically (temp file, then rename). Link task.
bool save(const char* name, const uint8_t* data, size_t len, char* err, size_t errLen);

// Loads /a/<name> into a new PSRAM buffer (free with heap_caps_free). 0 when missing.
size_t load(const char* name, uint8_t** out);

// Every stored file ({name, size}) and the free flash bytes, for ASSET_LIST_DATA.
void list(JsonDocument& out);

// The file an image widget of `w` x `h` px (its box) on a page with card color `bg` needs.
bool nameFor(const cfg::WidgetSpec& w, int boxW, int boxH, uint32_t bg, char* out, size_t n);

// Deletes files no image widget of `cfg` uses. UI task, after a config is applied.
void collectGarbage(const cfg::Config& cfg);

// Increases with every stored file (the UI rebuilds pages that were missing one).
uint32_t generation();

}  // namespace assets
}  // namespace rp
