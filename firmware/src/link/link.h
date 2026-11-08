// Host link over the USB CDC port (docs/protocol.md): framing, handshake, heartbeat, time sync.
// Runs as its own task on core 0. Other tasks send through a queue and read state through the
// thread-safe getters below; nothing here touches LVGL.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "rezpad_protocol.h"

namespace rp {
namespace link {

bool begin();

// HELLO done and PINGs arriving (lost after 3 s of silence).
bool hostConnected();
const char* hostVersion();  // valid while connected

// Queues a JSON message for the host. Small bodies only (up to kMaxQueuedBody bytes).
constexpr size_t kMaxQueuedBody = 480;
bool send(uint8_t type, const char* json);

// ACTION {"action": name, "value": value}.
bool sendAction(const char* action, int value);
// ACTION {"action": "macro", "value": 1, "id": id}: the host runs the macro.
bool sendMacro(const char* id);

// Wall clock from TIME_SYNC (kept by the ESP32 system clock afterwards).
bool timeSynced();
// Local time as minutes since midnight, or -1 when never synced.
int localMinutes();
// Unix time in seconds, or 0 when never synced.
int64_t unixNow();
// UTC offset of the PC's time zone in minutes (from TIME_SYNC).
int tzOffsetMin();
// Local calendar time; false when never synced.
bool localTime(struct tm& out);

struct Stats {
  uint32_t rxPackets;
  uint32_t txPackets;
  uint32_t txDropped;
  uint32_t unknownTypes;
  uint32_t connects;
  proto::DecoderStats decoder;
};
Stats stats();

}  // namespace link
}  // namespace rp
