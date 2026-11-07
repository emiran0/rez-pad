#include "link_monitor.h"

#include "rezpad_protocol.h"

namespace rp {
namespace proto {

bool LinkMonitor::onHello(uint32_t nowMs, uint8_t hostProto) {
  if (hostProto != kVersion) {
    reset();
    return false;
  }
  handshaken_ = true;
  needHelloSent_ = false;
  pinged_ = true;  // HELLO counts as a sign of life
  lastPingMs_ = nowMs;
  return true;
}

void LinkMonitor::onPing(uint32_t nowMs) {
  if (!handshaken_ && !pinged_) firstOrphanPingMs_ = nowMs;
  pinged_ = true;
  lastPingMs_ = nowMs;
}

bool LinkMonitor::update(uint32_t nowMs) {
  if (pinged_ && nowMs - lastPingMs_ > kTimeoutMs) {
    handshaken_ = false;
    pinged_ = false;
    needHelloSent_ = false;
  }
  const bool now = handshaken_ && pinged_;
  if (now == connected_) return false;
  connected_ = now;
  return true;
}

void LinkMonitor::reset() {
  handshaken_ = false;
  connected_ = false;
  pinged_ = false;
  needHelloSent_ = false;
}

bool LinkMonitor::takeNeedHello(uint32_t nowMs) {
  if (handshaken_ || !pinged_ || needHelloSent_) return false;
  if (nowMs - firstOrphanPingMs_ < kNeedHelloMs) return false;
  needHelloSent_ = true;
  return true;
}

}  // namespace proto
}  // namespace rp
