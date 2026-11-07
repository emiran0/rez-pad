// Device-side connection state (docs/protocol.md, "Connection rules"): the host is connected after
// a HELLO with a compatible protocol version, and lost after 3 s without a PING.
// Pure C++, unit-tested on the host.
#pragma once

#include <stdint.h>

namespace rp {
namespace proto {

class LinkMonitor {
 public:
  static constexpr uint32_t kTimeoutMs = 3000;
  // PINGs without a HELLO for this long: ask the host to say HELLO again.
  static constexpr uint32_t kNeedHelloMs = 1000;

  // Returns true if the host's protocol version is accepted.
  bool onHello(uint32_t nowMs, uint8_t hostProto);
  void onPing(uint32_t nowMs);

  // Call regularly. Returns true when the connected state changed.
  bool update(uint32_t nowMs);

  // The host app is gone or never said HELLO (USB unplugged, app closed, crashed).
  void reset();

  bool connected() const { return connected_; }

  // True once per episode of PINGs arriving without a HELLO (e.g. after a device reset while the
  // host kept the port open). The caller replies with ERROR "need_hello".
  bool takeNeedHello(uint32_t nowMs);

 private:
  bool handshaken_ = false;
  bool connected_ = false;
  bool pinged_ = false;
  bool needHelloSent_ = false;
  uint32_t lastPingMs_ = 0;
  uint32_t firstOrphanPingMs_ = 0;
};

}  // namespace proto
}  // namespace rp
