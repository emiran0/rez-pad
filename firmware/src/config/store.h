// Config store (docs/config-schema.md): the device's copy of the host's config in LittleFS
// (/config.json), validated on load with the built-in default as fallback, atomic saves, and the
// handoff of a newly received config from the link task to the UI task.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config_logic.h"

namespace rp {
namespace config {

// Mounts LittleFS and loads /config.json; on any problem uses the built-in default config and
// sets warning(). Call once in setup(), before anything reads active().
void begin();

// The config in use. Owned by the UI task: other tasks only read it during setup().
const cfg::Config& active();

// Why the built-in default is in use ("" when the stored config loaded fine, or none was stored).
const char* warning();

// CRC32 of the stored /config.json, 0 when none is stored (HELLO_ACK cfg_crc). Any task.
uint32_t storedCrc();

// PSRAM-backed Config objects (about 25 KB each).
cfg::Config* allocate();
void release(cfg::Config* c);

// Writes `json` to /config.json atomically (temp file, read back, rename). Link task.
bool save(const uint8_t* json, size_t len, char* err, size_t errLen);

// Link task -> UI task: a validated, saved config to apply. Ownership passes with the pointer; a
// config that was not taken yet is released.
void post(cfg::Config* c);
// UI task: returns a posted config or nullptr; the UI calls setActive() once it is applied.
cfg::Config* take();
void setActive(cfg::Config* c);  // releases the previous one

// Reads the stored file (or the built-in default) for CONFIG_GET. Returns the length, 0 on error.
size_t readStored(uint8_t* buf, size_t cap);

// Deletes /config.json (CMD factory_reset); the default applies after a reboot.
bool erase();

}  // namespace config
}  // namespace rp
