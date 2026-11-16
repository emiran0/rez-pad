#include "link.h"
#include "version_info.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <USBCDC.h>
#include <esp32-hal-tinyusb.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "asset_format.h"
#include "assets/assets.h"
#include "blob_assembler.h"
#include "config/store.h"
#include "config_logic.h"
#include "input/input.h"
#include "link_monitor.h"
#include "media/media.h"
#include "audio/mixer.h"
#include "audio/spectrum.h"
#include "sysstats/sysstats.h"
#include "weather/weather.h"
#include "usb_device.h"


namespace rp {
namespace link {

namespace {

using proto::Packet;

struct TxItem {
  uint8_t type;
  uint16_t len;
  char body[kMaxQueuedBody];
};

constexpr size_t kMaxReplyBody = 512;  // JSON replies built inside the link task
constexpr size_t kConfigChunk = 1024;  // CONFIG_DATA_CHUNK payload (device -> host)

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

// Largest outgoing body: ASSET_LIST_DATA (6 KB), then CONFIG_DATA_CHUNK.
uint8_t gFrame[proto::maxFrameSize(6144)];

// Config transfer (CONFIG_BEGIN / CHUNK / END) into a PSRAM buffer.
uint8_t* gCfgBuf = nullptr;
proto::BlobAssembler gCfgAsm(nullptr, 0);

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
  out["fw_ver"] = kFwVersion;
  // The running screen (display.panel / display.orientation of the config the device booted with).
  out["w"] = config::active().screenW;
  out["h"] = config::active().screenH;
  out["psram_free"] = ESP.getFreePsram();
  out["cfg_crc"] = config::storedCrc();  // 0 = nothing stored, running on the built-in default
  out["buttons"] = config::active().buttons;  // read-only use of the UI's config; a byte, no tearing
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

void writeConfigAck(bool ok, uint32_t crc, const char* error) {
  JsonDocument out;
  out["ok"] = ok;
  out["crc32"] = crc;
  out["error"] = error;
  writeJson(proto::kConfigAck, out);
  if (!ok) Serial.printf("[link] config rejected: %s\n", error);
}

void onConfigBegin(const Packet& p) {
  JsonDocument in;
  if (deserializeJson(in, p.body, p.bodyLen)) {
    writeConfigAck(false, 0, "config: CONFIG_BEGIN body is not valid JSON");
    return;
  }
  const uint32_t size = in["size"] | 0u;
  const uint32_t crc = in["crc32"] | 0u;
  if (gCfgAsm.begin(size, crc) != proto::BlobAssembler::Result::kOk) {
    char msg[64];
    snprintf(msg, sizeof(msg), "config: size %lu not accepted (max %u bytes)", static_cast<unsigned long>(size),
             static_cast<unsigned>(cfg::kMaxConfigBytes));
    writeConfigAck(false, crc, msg);
  }
}

void onConfigChunk(const Packet& p) {
  const bool wasActive = gCfgAsm.active();
  if (gCfgAsm.chunk(p.body, p.bodyLen) == proto::BlobAssembler::Result::kBadChunk && wasActive) {
    writeConfigAck(false, gCfgAsm.crc(), "config: transfer chunk out of order");
  }
}

void onConfigEnd() {
  using R = proto::BlobAssembler::Result;
  const R r = gCfgAsm.end();
  if (r == R::kIgnored) return;  // already answered (bad begin or chunk)
  if (r != R::kOk) {
    writeConfigAck(false, gCfgAsm.crc(), r == R::kBadCrc ? "config: CRC mismatch" : "config: transfer incomplete");
    return;
  }
  const uint32_t t0 = millis();
  cfg::Config* c = config::allocate();
  if (!c) {
    writeConfigAck(false, gCfgAsm.crc(), "config: out of memory");
    return;
  }
  char err[128];
  if (!cfg::parse(reinterpret_cast<const char*>(gCfgAsm.data()), gCfgAsm.size(), *c, err, sizeof(err)) ||
      !config::save(gCfgAsm.data(), gCfgAsm.size(), err, sizeof(err))) {
    config::release(c);
    writeConfigAck(false, gCfgAsm.crc(), err);
    return;
  }
  Serial.printf("[link] config %u bytes validated and saved in %lu ms\n", static_cast<unsigned>(gCfgAsm.size()),
                static_cast<unsigned long>(millis() - t0));
  config::post(c);  // the UI task applies it and sends CONFIG_ACK
}

// ---- assets (docs/protocol.md, "Assets") ----

uint8_t* gAssetBuf = nullptr;  // PSRAM, only while a transfer runs
proto::BlobAssembler gAssetAsm(nullptr, 0);
char gAssetName[proto::kAssetNameMax] = "";

void writeAssetAck(bool ok, const char* error) {
  JsonDocument out;
  out["ok"] = ok;
  out["name"] = gAssetName;
  out["error"] = error;
  writeJson(proto::kAssetAck, out);
  if (!ok) Serial.printf("[link] asset %s rejected: %s\n", gAssetName, error);
}

void freeAssetBuf() {
  if (gAssetBuf) heap_caps_free(gAssetBuf);
  gAssetBuf = nullptr;
  gAssetAsm = proto::BlobAssembler(nullptr, 0);
}

void onAssetBegin(const Packet& p) {
  freeAssetBuf();
  JsonDocument in;
  gAssetName[0] = '\0';
  if (deserializeJson(in, p.body, p.bodyLen)) {
    writeAssetAck(false, "ASSET_BEGIN body is not valid JSON");
    return;
  }
  strlcpy(gAssetName, in["name"] | "", sizeof(gAssetName));
  const uint32_t size = in["size"] | 0u;
  if (!assets::validName(gAssetName)) {
    writeAssetAck(false, "bad asset name");
    return;
  }
  if (size == 0 || size > proto::kMaxAssetBytes) {
    writeAssetAck(false, "bad size");
    return;
  }
  gAssetBuf = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!gAssetBuf) {
    writeAssetAck(false, "out of memory");
    return;
  }
  gAssetAsm = proto::BlobAssembler(gAssetBuf, size);
  gAssetAsm.begin(size, in["crc32"] | 0u);
}

void onAssetChunk(const Packet& p) {
  const bool wasActive = gAssetAsm.active();
  if (gAssetAsm.chunk(p.body, p.bodyLen) == proto::BlobAssembler::Result::kBadChunk && wasActive) {
    writeAssetAck(false, "chunk out of order");
    freeAssetBuf();
  }
}

void onAssetEnd() {
  using R = proto::BlobAssembler::Result;
  const R r = gAssetAsm.end();
  if (r == R::kIgnored) return;  // already answered
  char err[96] = "";
  bool ok = r == R::kOk;
  if (!ok) snprintf(err, sizeof(err), "%s", r == R::kBadCrc ? "CRC mismatch" : "transfer incomplete");
  const uint32_t t0 = millis();
  if (ok) ok = assets::save(gAssetName, gAssetAsm.data(), gAssetAsm.size(), err, sizeof(err));
  if (ok) {
    Serial.printf("[link] asset %s: %u bytes saved in %lu ms\n", gAssetName, static_cast<unsigned>(gAssetAsm.size()),
                  static_cast<unsigned long>(millis() - t0));
  }
  freeAssetBuf();
  writeAssetAck(ok, err);
}

void onAssetList() {
  JsonDocument out;
  assets::list(out);
  // Up to 8 x 16 files of about 40 bytes each fit a frame comfortably.
  constexpr size_t kMax = 6144;
  char* buf = static_cast<char*>(heap_caps_malloc(kMax, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) return;
  const size_t len = serializeJson(out, buf, kMax);
  writeFrame(proto::kAssetListData, reinterpret_cast<const uint8_t*>(buf), len);
  heap_caps_free(buf);
}

// CONFIG_GET: the stored config (or the built-in default) as CONFIG_DATA_BEGIN / CHUNK / END.
void onConfigGet() {
  uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(cfg::kMaxConfigBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  const size_t n = buf ? config::readStored(buf, cfg::kMaxConfigBytes) : 0;
  JsonDocument head;
  head["size"] = n;
  head["crc32"] = proto::crc32(buf, n);
  writeJson(proto::kConfigDataBegin, head);
  static uint8_t chunk[4 + kConfigChunk];
  for (size_t off = 0; off < n; off += kConfigChunk) {
    const size_t len = n - off < kConfigChunk ? n - off : kConfigChunk;
    for (int i = 0; i < 4; ++i) chunk[i] = static_cast<uint8_t>(off >> (8 * i));
    memcpy(chunk + 4, buf + off, len);
    writeFrame(proto::kConfigDataChunk, chunk, 4 + len);
  }
  writeFrame(proto::kConfigDataEnd, nullptr, 0);
  heap_caps_free(buf);
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
  if (strcmp(cmd, "factory_reset") == 0) {
    // Deletes the stored config; the built-in default applies after the restart. Until the host
    // pushes its config again (it will on connect, the CRC differs).
    out["ok"] = config::erase();
    writeJson(proto::kCmdResult, out);
    usb::cdc().flush();
    delay(100);
    ESP.restart();
  }
  if (strcmp(cmd, "input_learn") == 0) {
    // Numbering from the web page: the next control turned or pressed is reported with
    // INPUT_LEARNED instead of acting. args.ms: how long to wait (0 cancels, at most 60 s).
    long ms = in["args"]["ms"] | 10000L;
    if (ms < 0) ms = 0;
    if (ms > 60000) ms = 60000;
    input::startLearn(static_cast<uint32_t>(ms));
    out["ok"] = true;
    writeJson(proto::kCmdResult, out);
    return;
  }
  if (strcmp(cmd, "bootloader") == 0) {
    // Firmware update from the host: restart into the ROM download mode on this same USB port
    // (USB-Serial/JTAG, 303A:1001). The host writes the image with the ROM loader protocol and
    // resets the chip; config and images in LittleFS are not touched.
    out["ok"] = true;
    writeJson(proto::kCmdResult, out);
    usb::cdc().flush();
    delay(150);
    Serial.println("[link] restarting into the ROM download mode for a firmware update");
    usb_persist_restart(RESTART_BOOTLOADER);
    return;  // only if the restart could not be arranged
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
    case proto::kSpectrum: spectrum::onSpectrum(p.body, p.bodyLen); break;
    case proto::kMixer: mixer::onMixer(p.body, p.bodyLen); break;
    case proto::kWeather: weather::onWeather(p.body, p.bodyLen); break;
    case proto::kConfigBegin: onConfigBegin(p); break;
    case proto::kConfigChunk: onConfigChunk(p); break;
    case proto::kConfigEnd: onConfigEnd(); break;
    case proto::kConfigGet: onConfigGet(); break;
    case proto::kAssetBegin: onAssetBegin(p); break;
    case proto::kAssetChunk: onAssetChunk(p); break;
    case proto::kAssetEnd: onAssetEnd(); break;
    case proto::kAssetList: onAssetList(); break;
    default:
      gStats.unknownTypes++;
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
    if (!gConnected && input::learning()) input::startLearn(0);  // nobody waits for it any more
    input::Learned learned;
    if (input::takeLearned(learned) && gConnected) {
      char buf[64];
      if (learned.port) snprintf(buf, sizeof(buf), "{\"kind\":\"%s\",\"port\":%u}", learned.knob ? "knob" : "button", learned.port);
      else snprintf(buf, sizeof(buf), "{\"kind\":null,\"port\":0}");
      writeFrame(proto::kInputLearned, reinterpret_cast<const uint8_t*>(buf), strlen(buf));
      busy = true;
    }

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
  gCfgBuf = static_cast<uint8_t*>(heap_caps_malloc(cfg::kMaxConfigBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  gCfgAsm = proto::BlobAssembler(gCfgBuf, gCfgBuf ? cfg::kMaxConfigBytes : 0);
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

bool sendActionId(const char* action, int value, const char* id) {
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"action\":\"%s\",\"value\":%d,\"id\":\"%s\"}", action, value, id);
  return send(proto::kAction, buf);
}

bool sendMacro(const char* id) {
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"action\":\"macro\",\"value\":1,\"id\":\"%s\"}", id);
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

int64_t unixNow() { return gTimeSynced ? static_cast<int64_t>(time(nullptr)) : 0; }

int tzOffsetMin() { return gTzOffsetMin; }

bool localTime(struct tm& out) {
  if (!gTimeSynced) return false;
  const time_t local = time(nullptr) + static_cast<time_t>(gTzOffsetMin) * 60;
  gmtime_r(&local, &out);
  return true;
}

Stats stats() {
  Stats s = gStats;
  s.decoder = gDecoder.stats();
  return s;
}

}  // namespace link
}  // namespace rp
