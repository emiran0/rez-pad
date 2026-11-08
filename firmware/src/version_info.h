// The release version (repo VERSION file) and git commit, e.g. "0.2.0+913c346" (".dirty" with
// uncommitted changes). Defined in the generated version_info.cpp (scripts/version.py).
#pragma once

#include <stdint.h>

namespace rp {
extern const char* const kFwVersion;
extern const uint16_t kFwVersionBcd;  // USB bcdDevice
}  // namespace rp
