#include "link.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <USBCDC.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "config/defaults.h"
#include "link_monitor.h"
#include "media/media.h"
#include "sysstats/sysstats.h"
#include "usb_device.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

namespace rp {
namespace link {

namespace {

using proto::Packet;

struct TxItem {
  uint8_t type;
  uint16_t len;
  char body[kMaxQueuedBody];
};

constexpr size_t kMaxReplyBody = 512;  // replies built inside the link task

QueueHandle_t gTxQueue = nullptr;
proto::FrameDecoder gDecoder;
proto::LinkMonitor gMonitor;
uint16_t gSeq = 0;

portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED;
volatile bool gConnected = false;
char gHostVer[24] = "";
volatile bool gTimeSynced = false;
volatile int32_t gTzOffsetMin = 0;
Stats gStats = {};

uint8_t gFrame[proto::maxFrameSize(kMaxReplyBody)];

// Link task only.
void writeFrame(uint8_t type, const uint8_t* body, size_t len) {
  USBCDC& cdc = usb::cdc();
  // write() itself drops data while the host has not raised DTR (port not open).
  const size_t n = proto::buildFrame(type, gSeq++, body, len, gFrame, sizeof(gFrame));
  if (n == 0) return;
  if (cdc.write(gFrame, n) == n) {
    gStats.txPackets++;
  } else {
    gStats.txDropped++;
  }
}

void writeJson(uint8_t type, const JsonDocument& doc) {
  char buf[kMaxReplyBody];
  const size_t len = serializeJson(doc, buf, sizeof(buf));
  writeFrame(type, reinterpret_cast<const uint8_t*>(buf), len);
}

void writeError(const char* code, const char* msg) {
  JsonDocument doc;
  doc["code"] = code;
  doc["msg"] = msg;
  writeJson(proto::kError, doc);
}

void onHello(const Packet& p, uint32_t now) {
  JsonDocument in;
  if (deserializeJson(in, p.body, p.bodyLen)) {
    writeError("bad_json", "HELLO body is not valid JSON");
    return;
  }
  const int hostProto = in["proto"] | 0;
  if (!gMonitor.onHello(now, static_cast<uint8_t>(hostProto))) {
    char msg[96];
    snprintf(msg, sizeof(msg), "firmware speaks protocol v%u, host sent v%d", proto::kVersion, hostProto);
    writeError("proto_mismatch", msg);
    Serial.printf("[link] %s\n", msg);
    return;
  }
  taskENTER_CRITICAL(&gLock);
  strlcpy(gHostVer, in["host_ver"] | "?", sizeof(gHostVer));
  taskEXIT_CRITICAL(&gLock);

  JsonDocument out;
  out["proto"] = proto::kVersion;
  out["fw_ver"] = FW_VERSION;
  out["w"] = 480;
  out["h"] = 320;
  out["psram_free"] = ESP.getFreePsram();
  out["cfg_crc"] = 0;  // config store arrives in Phase 9
  out["buttons"] = defaults::kButtons;
  out["serial"] = usb::serialNumber();
  if (media::artSize() > 0) out["art"] = media::artSize();  // album art edge the layout wants
  writeJson(proto::kHelloAck, out);
}

void onTimeSync(const Packet& p) {
  JsonDocument in;
  if (deserializeJson(in, p.body, p.bodyLen)) return;
  const int64_t unixMs = in["unix_ms"] | static_cast<int64_t>(0);
  if (unixMs <= 0) return;
  timeval tv;
  tv.tv_sec = static_cast<time_t>(unixMs / 1000);
  tv.tv_usec = static_cast<suseconds_t>((unixMs % 1000) * 1000);
  settimeofday(&tv, nullptr);
  gTzOffsetMin = in["tz_offset_min"] | 0;
  gTimeSynced = true;
}

void onCmd(const Packet& p) {
  JsonDocument in;
  if (deserializeJson(in, p.body, p.bodyLen)) return;
  const char* cmd = in["cmd"] | "";
  JsonDocument out;
  out["cmd"] = cmd;
  if (strcmp(cmd, "reboot") == 0) {
    out["ok"] = true;
    writeJson(proto::kCmdResult, out);
    usb::cdc().flush();
    delay(100);
    ESP.restart();
  }
  out["ok"] = false;
  out["data"] = "not implemented";
  writeJson(proto::kCmdResult, out);
}

void dispatch(const Packet& p, uint32_t now) {
  gStats.rxPackets++;
  switch (p.type) {
    case proto::kHello: onHello(p, now); break;
    case proto::kPing:
      gMonitor.onPing(now);
      writeFrame(proto::kPong, nullptr, 0);
      break;
    case proto::kTimeSync: onTimeSync(p); break;
    case proto::kCmd: onCmd(p); break;
    case proto::kNowPlaying: media::onNowPlaying(p.body, p.bodyLen); break;
    case proto::kVolume: media::onVolume(p.body, p.bodyLen); break;
    case proto::kArtBegin: media::onArtBegin(p.body, p.bodyLen); break;
    case proto::kArtChunk: media::onArtChunk(p.body, p.bodyLen); break;
    case proto::kArtEnd: media::onArtEnd(p.body, p.bodyLen); break;
    case proto::kArtNone: media::onArtNone(p.body, p.bodyLen); break;
    case proto::kSysStats: sysstats::onSysStats(p.body, p.bodyLen); break;
    default:
      gStats.unknownTypes++;  // WEATHER, CONFIG_*: later phases
      break;
  }
}

void task(void*) {
  static uint8_t rx[512];
  for (;;) {
    bool busy = false;
    USBCDC& cdc = usb::cdc();
    const uint32_t now = millis();

    int avail = cdc.available();
    if (avail > 0) {
      const size_t n = cdc.read(rx, avail < static_cast<int>(sizeof(rx)) ? avail : sizeof(rx));
      for (size_t i = 0; i < n; ++i) {
        if (gDecoder.push(rx[i])) dispatch(gDecoder.packet(), now);
      }
      busy = true;
    }

    if (!usb::mounted()) gMonitor.reset();  // unplugged or PC asleep; app exit is caught by the PING timeout
    if (gMonitor.update(now)) {
      gConnected = gMonitor.connected();
      if (gConnected) gStats.connects++;
      else media::onHostLost();
      Serial.printf("[link] host %s\n", gConnected ? "connected" : "lost");
    }
    if (gMonitor.takeNeedHello(now)) writeError("need_hello", "device restarted, send HELLO");

    TxItem item;
    while (xQueueReceive(gTxQueue, &item, 0) == pdTRUE) {
      if (gConnected) writeFrame(item.type, reinterpret_cast<const uint8_t*>(item.body), item.len);
      busy = true;
    }

    if (!busy) vTaskDelay(1);
  }
}

}  // namespace

bool begin() {
  gTxQueue = xQueueCreate(8, sizeof(TxItem));
  if (!gTxQueue) return false;
  return xTaskCreatePinnedToCore(task, "link", 8192, nullptr, 4, nullptr, 0) == pdPASS;
}

bool hostConnected() { return gConnected; }

const char* hostVersion() { return gHostVer; }

bool send(uint8_t type, const char* json) {
  if (!gTxQueue || !gConnected) return false;
  TxItem item;
  item.type = type;
  const size_t len = strlen(json);
  if (len > sizeof(item.body)) return false;
  memcpy(item.body, json, len);
  item.len = static_cast<uint16_t>(len);
  if (xQueueSend(gTxQueue, &item, 0) != pdTRUE) {
    gStats.txDropped++;
    return false;
  }
  return true;
}

bool sendAction(const char* action, int value) {
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"action\":\"%s\",\"value\":%d}", action, value);
  return send(proto::kAction, buf);
}

bool timeSynced() { return gTimeSynced; }

int localMinutes() {
  if (!gTimeSynced) return -1;
  const int64_t local = static_cast<int64_t>(time(nullptr)) + static_cast<int64_t>(gTzOffsetMin) * 60;
  int64_t m = (local / 60) % 1440;
  if (m < 0) m += 1440;
  return static_cast<int>(m);
}

Stats stats() {
  Stats s = gStats;
  s.decoder = gDecoder.stats();
  return s;
}

}  // namespace link
}  // namespace rp
