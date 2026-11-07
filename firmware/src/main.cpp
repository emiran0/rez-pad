// rez-pad firmware entry point.
// Phase 6: Now Playing from the host (track, progress, album art, volume). Native USB is a
// composite device (CDC + HID media keys); media actions go to the host app when it is connected,
// otherwise out as media keys. Phase 7: system stats from the host. Phase 8: weather from the host, room
// climate sent back as SENSOR, clock widget. Phase 9: pages, inputs and settings from the config
// (LittleFS /config.json, pushed by the host; built-in default when missing or invalid).
// The Phase 2 debug view and Phase 1 display tools stay available from the debug console on UART0 (the USB-UART bridge port, 115200 baud); they pause LVGL while they draw.
#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <string.h>

#include "app/actions.h"
#include "app/settings.h"
#include "config/store.h"
#include "debug/console.h"
#include "debug/wiring_check.h"
#include "display/backlight.h"
#include "display/display.h"
#include "hid/media_keys.h"
#include "input/input.h"
#include "link/link.h"
#include "link/usb_device.h"
#include "media/media.h"
#include "sysstats/sysstats.h"
#include "weather/weather.h"
#include "pins.h"
#include "rezpad_protocol.h"
#include "sensors/sensors.h"
#include "ui/debug_view.h"
#include "ui/lvgl_port.h"
#include "ui/model.h"
#include "ui/ui.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

namespace {

constexpr uint32_t kViewRefreshMs = 50;

// Direct LCD drawing (test pattern, fills, benchmark): pause LVGL so it does not draw over it.
void rawView() {
  rp::debug_view::hide();
  rp::lvgl_port::setPaused(true);
}

bool protocolSelfTest() {
  static const uint8_t kCheck[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  return rp::proto::crc16(kCheck, sizeof(kCheck)) == 0x29B1 &&
         rp::proto::crc32(kCheck, sizeof(kCheck)) == 0xCBF43926u;
}

void printInfo(Print& out) {
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  uint32_t flashSize = 0;
  esp_flash_get_size(nullptr, &flashSize);

  out.printf("fw %s, protocol v%u, built %s %s\n", FW_VERSION, rp::proto::kVersion, __DATE__, __TIME__);
  out.printf("chip ESP32-S3 rev %u.%u, %u cores, %lu MHz\n", static_cast<unsigned>(chip.revision / 100),
             static_cast<unsigned>(chip.revision % 100), static_cast<unsigned>(chip.cores),
             static_cast<unsigned long>(getCpuFrequencyMhz()));
  out.printf("flash %lu MB\n", static_cast<unsigned long>(flashSize / (1024 * 1024)));
  if (psramFound()) {
    out.printf("PSRAM %lu KB total, %lu KB free\n", static_cast<unsigned long>(ESP.getPsramSize() / 1024),
               static_cast<unsigned long>(ESP.getFreePsram() / 1024));
  } else {
    out.println("PSRAM NOT FOUND (check board/memory_type settings)");
  }
  out.printf("internal heap %lu KB free\n", static_cast<unsigned long>(ESP.getFreeHeap() / 1024));
  out.printf("protocol self-test %s\n", protocolSelfTest() ? "ok" : "FAILED");
  out.printf("display %dx%d, SPI %.1f MHz, backlight %u%%\n", static_cast<int>(rp::display::lcd().width()),
             static_cast<int>(rp::display::lcd().height()), rp::display::effectiveSpiHz(rp::display::spiHz()) / 1e6,
             rp::backlight::get());
}

bool parseFloat(const char* s, float lo, float hi, float& out) {
  char* end = nullptr;
  const float v = strtof(s, &end);
  if (end == s || *end != '\0' || v < lo || v > hi) return false;
  out = v;
  return true;
}

bool parseUint(const char* s, long lo, long hi, long& out) {
  char* end = nullptr;
  const long v = strtol(s, &end, 10);
  if (end == s || *end != '\0' || v < lo || v > hi) return false;
  out = v;
  return true;
}

// ---- console commands ----

void cmdInfo(int, char**, Print& out) { printInfo(out); }

void cmdI2c(int, char**, Print& out) { rp::wiring::scanI2c(out); }

void cmdPins(int, char**, Print& out) { rp::wiring::printInputLevels(out); }

void cmdBacklight(int argc, char** argv, Print& out) {
  long v = 0;
  if (argc == 2 && parseUint(argv[1], 0, 100, v)) rp::backlight::set(static_cast<uint8_t>(v));
  else if (argc != 1) out.println("usage: bl <0-100>");
  out.printf("backlight %u%%\n", rp::backlight::get());
}

void cmdSpi(int argc, char** argv, Print& out) {
  long mhz = 0;
  if (argc == 2 && parseUint(argv[1], 1, 80, mhz)) {
    rp::display::setSpiHz(static_cast<uint32_t>(mhz) * 1000000u);
    rawView();
    rp::display::drawTestPattern();
  } else if (argc != 1) {
    out.println("usage: spi <1-80>  (MHz, redraws the test pattern)");
  }
  out.printf("SPI requested %.1f MHz, effective %.1f MHz\n", rp::display::spiHz() / 1e6,
             rp::display::effectiveSpiHz(rp::display::spiHz()) / 1e6);
}

void cmdPattern(int, char**, Print& out) {
  rawView();
  const uint32_t t0 = micros();
  rp::display::drawTestPattern();
  out.printf("test pattern drawn in %.1f ms\n", (micros() - t0) / 1000.0f);
}

void cmdFill(int argc, char** argv, Print& out) {
  struct Named {
    const char* name;
    uint32_t color;
  };
  static const Named kColors[] = {{"black", TFT_BLACK}, {"white", TFT_WHITE}, {"red", TFT_RED},
                                  {"green", TFT_GREEN}, {"blue", TFT_BLUE},   {"grey", TFT_DARKGREY}};
  if (argc == 2) {
    for (const auto& c : kColors) {
      if (strcmp(argv[1], c.name) == 0) {
        rawView();
        rp::display::lcd().fillScreen(c.color);
        return;
      }
    }
  }
  out.println("usage: fill <black|white|red|green|blue|grey>");
}

void cmdRotation(int argc, char** argv, Print& out) {
  long r = 0;
  if (argc == 2 && parseUint(argv[1], 0, 3, r)) {
    rp::display::setRotation(static_cast<uint8_t>(r));
    rawView();
    rp::display::drawTestPattern();
  } else {
    out.println("usage: rot <0-3>  (1 and 3 are landscape)");
  }
}

void cmdInvert(int argc, char** argv, Print& out) {
  long v = 0;
  if (argc == 2 && parseUint(argv[1], 0, 1, v)) rp::display::setInvert(v != 0);
  else out.println("usage: lcdinv <0|1>");
}

void cmdBench(int argc, char** argv, Print& out) {
  static const uint32_t kDefault[] = {20000000, 40000000, 60000000, 80000000};
  uint32_t list[6];
  size_t n = 0;
  for (int i = 1; i < argc && n < 6; ++i) {
    long mhz = 0;
    if (!parseUint(argv[i], 1, 80, mhz)) {
      out.println("usage: bench [MHz ...]  (default 20 40 60 80)");
      return;
    }
    list[n++] = static_cast<uint32_t>(mhz) * 1000000u;
  }
  rawView();
  if (n == 0) rp::display::runBenchmark(kDefault, 4, out);
  else rp::display::runBenchmark(list, n, out);
}

void cmdView(int argc, char** argv, Print& out) {
  if (argc == 2 && strcmp(argv[1], "ui") == 0) {
    rp::debug_view::hide();
    rp::lvgl_port::setPaused(false);
  } else if (argc == 2 && strcmp(argv[1], "debug") == 0) {
    rp::lvgl_port::setPaused(true);
    rp::debug_view::show();
  } else if (argc == 2 && strcmp(argv[1], "pattern") == 0) {
    cmdPattern(1, argv, out);
  } else {
    out.println("usage: view <ui|debug|pattern>");
  }
}

void cmdAction(int argc, char** argv, Print& out) {
  if (argc < 2) {
    out.println("usage: action <name> [value]   e.g. action media.next, action media.volume -3");
    return;
  }
  rp::actions::Action a;
  if (!rp::actions::parse(argv[1], a)) {
    out.printf("unknown action '%s'\n", argv[1]);
    return;
  }
  const int value = argc >= 3 ? atoi(argv[2]) : 1;
  rp::actions::run(a, value);
  out.printf("ran %s %d\n", rp::actions::name(a), value);
}

void cmdPage(int argc, char** argv, Print& out) {
  long n = 0;
  if (argc == 2 && parseUint(argv[1], 0, 7, n)) rp::ui::showPage(static_cast<int>(n));
  else out.println("usage: page <index>");
}

void cmdPerf(int argc, char** argv, Print& out) {
  long v = 0;
  if (argc == 2 && parseUint(argv[1], 0, 1, v)) rp::lvgl_port::showPerfMonitor(v != 0);
  else out.println("usage: perf <0|1>  (LVGL FPS/CPU overlay)");
}

void cmdSensor(int, char**, Print& out) {
  const auto r = rp::sensors::latest();
  if (r.updatedMs == 0) {
    out.println("sensor: no reading yet");
    return;
  }
  out.printf("room temp %.2f C, hum %.1f %%, press %.2f hPa, trend %s (%+.2f hPa/3h)\n", r.tempC, r.humPct,
             r.pressHpa, rp::sensor::trendName(r.trend), r.trendDelta3h);
  out.printf("raw: aht20 %.2f C %.1f %%, bmp280 %.2f C %.2f hPa (0x%02X)\n", r.rawTempC, r.rawHumPct, r.bmpTempC,
             r.rawPressHpa, r.bmpAddr);
  out.printf("ok aht %d bmp %d, samples %lu, errors aht %lu bmp %lu, age %lus\n", r.ahtOk, r.bmpOk,
             static_cast<unsigned long>(r.samples), static_cast<unsigned long>(r.ahtErrors),
             static_cast<unsigned long>(r.bmpErrors), static_cast<unsigned long>((millis() - r.updatedMs) / 1000));
}

void cmdOffset(int argc, char** argv, Print& out) {
  auto c = rp::sensors::config();
  float t = 0, h = 0;
  if (argc == 3 && parseFloat(argv[1], -10, 10, t) && parseFloat(argv[2], -20, 20, h)) {
    c.tempOffsetC = t;
    c.humOffsetPct = h;
    rp::sensors::setConfig(c);
    out.println("applied from the next reading (within 5 s)");
  } else if (argc != 1) {
    out.println("usage: offset <temp C> <hum %>");
  }
  out.printf("offsets: temp %+.2f C, hum %+.2f %%\n", c.tempOffsetC, c.humOffsetPct);
}

void cmdEnc(int, char**, Print& out) {
  const auto s = rp::input::snapshot();
  const auto& c = rp::input::config();
  for (int i = 0; i < 2; ++i) {
    out.printf("enc%d: count %ld, detents %ld, %d counts/click, invert %d, switch %s\n", i + 1,
               static_cast<long>(s.rawCount[i]), static_cast<long>(s.detents[i]), c.enc[i].countsPerDetent,
               c.enc[i].invert, s.switchDown[i] ? "down" : "up");
  }
  out.printf("buttons fitted %u, queue drops %lu\n", c.buttons, static_cast<unsigned long>(s.dropped));
}

void cmdCpd(int argc, char** argv, Print& out) {
  long e = 0, n = 0;
  if (argc == 3 && parseUint(argv[1], 1, 2, e) && parseUint(argv[2], 1, 8, n)) {
    auto c = rp::input::config();
    c.enc[e - 1].countsPerDetent = static_cast<int>(n);
    rp::input::setConfig(c);
  } else {
    out.println("usage: cpd <1|2> <counts per click>");
  }
  cmdEnc(1, argv, out);
}

void cmdEncInv(int argc, char** argv, Print& out) {
  long e = 0, v = 0;
  if (argc == 3 && parseUint(argv[1], 1, 2, e) && parseUint(argv[2], 0, 1, v)) {
    auto c = rp::input::config();
    c.enc[e - 1].invert = v != 0;
    rp::input::setConfig(c);
  } else {
    out.println("usage: encinv <1|2> <0|1>");
  }
  cmdEnc(1, argv, out);
}

void cmdButtons(int argc, char** argv, Print& out) {
  long n = 0;
  if (argc == 2 && parseUint(argv[1], 0, 2, n)) {
    auto c = rp::input::config();
    c.buttons = static_cast<uint8_t>(n);
    rp::input::setConfig(c);
  } else {
    out.println("usage: buttons <0-2>");
  }
  cmdEnc(1, argv, out);
}

void cmdUsb(int, char**, Print& out) {
  out.printf("USB %04X:%04X serial %s, %s%s\n", rp::usb::kVid, rp::usb::kPid, rp::usb::serialNumber(),
             rp::usb::mounted() ? "connected" : "not connected", rp::usb::suspended() ? " (suspended)" : "");
  out.printf("HID media keys sent %lu, dropped %lu\n", static_cast<unsigned long>(rp::hid::sentCount()),
             static_cast<unsigned long>(rp::hid::droppedCount()));
}

void cmdLink(int, char**, Print& out) {
  const auto s = rp::link::stats();
  out.printf("host %s%s%s, time %s\n", rp::link::hostConnected() ? "connected (host " : "not connected",
             rp::link::hostConnected() ? rp::link::hostVersion() : "", rp::link::hostConnected() ? ")" : "",
             rp::link::timeSynced() ? "synced" : "not synced");
  out.printf("rx %lu, tx %lu, tx dropped %lu, unknown types %lu, connects %lu\n",
             static_cast<unsigned long>(s.rxPackets), static_cast<unsigned long>(s.txPackets),
             static_cast<unsigned long>(s.txDropped), static_cast<unsigned long>(s.unknownTypes),
             static_cast<unsigned long>(s.connects));
  out.printf("frames bad: cobs %lu, short %lu, large %lu, version %lu, crc %lu\n",
             static_cast<unsigned long>(s.decoder.badCobs), static_cast<unsigned long>(s.decoder.tooShort),
             static_cast<unsigned long>(s.decoder.tooLarge), static_cast<unsigned long>(s.decoder.badVersion),
             static_cast<unsigned long>(s.decoder.badCrc));
  const int m = rp::link::localMinutes();
  if (m >= 0) out.printf("local time %02d:%02d\n", m / 60, m % 60);
}

void cmdMedia(int, char**, Print& out) {
  const auto s = rp::media::stats();
  out.printf("NOW_PLAYING received %lu, art ok %lu failed %lu, bad json %lu\n", static_cast<unsigned long>(s.nowPlaying),
             static_cast<unsigned long>(s.artOk), static_cast<unsigned long>(s.artFailed),
             static_cast<unsigned long>(s.badJson));
  out.printf("last art %lu bytes, decoded in %lu us, art size %d px, transfer %s\n",
             static_cast<unsigned long>(s.lastArtBytes), static_cast<unsigned long>(s.lastDecodeUs),
             rp::media::artSize(), rp::media::artTransferActive() ? "in progress" : "idle");
}

void cmdStats(int, char**, Print& out) {
  out.printf("SYS_STATS received %lu, sources dropped %lu, WEATHER received %lu\n",
             static_cast<unsigned long>(rp::sysstats::received()), static_cast<unsigned long>(rp::sysstats::dropped()),
             static_cast<unsigned long>(rp::weather::received()));
  const auto& wx = rp::ui::model().weather;
  if (wx.valid) {
    out.printf("  weather %s %.1f, code %u, %u days, %u hours, updated %lld\n", wx.loc, wx.temp, wx.code, wx.dayCount,
               wx.hourCount, static_cast<long long>(wx.updated));
  }
  // What the widgets show right now (the console runs on the UI loop, so reading the model is safe).
  for (const auto& st : rp::ui::model().stats) {
    if (!st.id[0]) continue;
    if (st.valid) out.printf("  %s = %.1f\n", st.id, st.value);
    else out.printf("  %s = --\n", st.id);
  }
}

void cmdConfig(int, char**, Print& out) {
  const auto& c = rp::config::active();
  out.printf("stored crc %08lX%s\n", static_cast<unsigned long>(rp::config::storedCrc()),
             rp::config::storedCrc() ? "" : " (none stored, built-in default)");
  if (rp::config::warning()[0]) out.printf("warning: %s\n", rp::config::warning());
  out.printf("grid %ux%u, brightness %u%%, spi %lu Hz, accent #%06lX, buttons %u, cpd %u/%u\n", c.cols, c.rows,
             c.brightness, static_cast<unsigned long>(c.spiHz), static_cast<unsigned long>(c.accent), c.buttons,
             c.countsPerDetent[0], c.countsPerDetent[1]);
  for (int p = 0; p < c.pageCount; ++p) {
    out.printf("page %d %s \"%s\": %u widgets\n", p, c.pages[p].id, c.pages[p].name, c.pages[p].widgetCount);
    for (int i = 0; i < c.pages[p].widgetCount; ++i) {
      const auto& w = c.pages[p].widgets[i];
      out.printf("  %s %u,%u %ux%u %s\n", w.type, w.x, w.y, w.w, w.h, w.source);
    }
  }
}

const rp::console::Command kCommands[] = {
    {"info", "", cmdInfo},
    {"i2c", "", cmdI2c},
    {"pins", "", cmdPins},
    {"bl", "<0-100>", cmdBacklight},
    {"spi", "<MHz>", cmdSpi},
    {"pattern", "", cmdPattern},
    {"fill", "<color>", cmdFill},
    {"rot", "<0-3>", cmdRotation},
    {"lcdinv", "<0|1>", cmdInvert},
    {"bench", "[MHz ...]", cmdBench},
    {"view", "<ui|debug|pattern>", cmdView},
    {"action", "<name> [value]", cmdAction},
    {"page", "<index>", cmdPage},
    {"perf", "<0|1>", cmdPerf},
    {"usb", "", cmdUsb},
    {"link", "", cmdLink},
    {"media", "", cmdMedia},
    {"stats", "", cmdStats},
    {"config", "", cmdConfig},
    {"enc", "", cmdEnc},
    {"cpd", "<1|2> <counts per click>", cmdCpd},
    {"encinv", "<1|2> <0|1>", cmdEncInv},
    {"buttons", "<0-2>", cmdButtons},
    {"sensor", "", cmdSensor},
    {"offset", "[<temp C> <hum %>]", cmdOffset},
};

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("hello from rez-pad");
  // Start USB first so Windows can enumerate while the rest initializes.
  const bool usbOk = rp::usb::begin();
  rp::config::begin();  // before the link task (HELLO_ACK reports the stored config's CRC)
  const bool mediaOk = rp::media::begin();  // before the link task, which feeds it
  const bool linkOk = rp::link::begin();

  rp::backlight::begin();  // off while the panel initializes, avoids a white flash
  const bool lcdOk = rp::display::begin();

  const auto& cfg = rp::config::active();
  if (rp::display::spiHz() != cfg.spiHz) rp::display::setSpiHz(cfg.spiHz);
  const bool inputOk = rp::input::begin(rp::settings::inputConfig(cfg));
  const rp::sensors::Config sCfg = rp::settings::sensorConfig(cfg);

  printInfo(Serial);
  if (!usbOk) Serial.println("USB init FAILED");
  if (!linkOk) Serial.println("link task FAILED");
  if (!mediaOk) Serial.println("media buffers FAILED");
  Serial.printf("USB %04X:%04X serial %s\n", rp::usb::kVid, rp::usb::kPid, rp::usb::serialNumber());
  if (!lcdOk) Serial.println("display init FAILED");
  if (!inputOk) Serial.println("input init FAILED");
  rp::wiring::scanI2c(Serial);  // before the sensor task starts using the bus
  rp::wiring::printInputLevels(Serial);
  if (!rp::sensors::begin(sCfg)) Serial.println("sensor task FAILED");

  if (!rp::ui::begin()) Serial.println("UI (LVGL) init FAILED");
  rp::ui::loop(millis());  // first frame before the backlight comes on
  rp::actions::setBrightness(cfg.brightness);

  rp::console::begin(kCommands, sizeof(kCommands) / sizeof(kCommands[0]));
  Serial.println("ready, type help");
}

void loop() {
  rp::console::poll();

  rp::input::Event e;
  while (rp::input::poll(e)) {
    if (e.kind == rp::input::Kind::kRotate || e.kind == rp::input::Kind::kRotatePressed) {
      Serial.printf("[input] %s %s %+d\n", rp::input::sourceName(e.source), rp::input::kindName(e.kind), e.value);
    } else {
      Serial.printf("[input] %s %s\n", rp::input::sourceName(e.source), rp::input::kindName(e.kind));
    }
    rp::debug_view::addEvent(e);
    if (!rp::debug_view::active()) rp::actions::handle(e);
  }

  const uint32_t now = millis();
  if (rp::debug_view::active()) {
    static uint32_t lastView = 0;
    if (now - lastView >= kViewRefreshMs) {
      lastView = now;
      rp::debug_view::update(rp::input::snapshot(), rp::sensors::latest());
    }
  }
  rp::ui::loop(now);
  delay(2);
}
