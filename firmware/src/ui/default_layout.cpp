#include "default_layout.h"

namespace rp {
namespace ui {

namespace {

using widgets::Spec;

Spec metric(const char* type, uint8_t x, uint8_t y, uint8_t w, uint8_t h, const char* source, const char* label,
            const char* unit, float min, float max, float warn, float crit) {
  Spec s{type, x, y, w, h};
  s.source = source;
  s.label = label;
  s.unit = unit;
  s.min = min;
  s.max = max;
  s.warn = warn;
  s.crit = crit;
  return s;
}

const Spec kNowPlaying[] = {
    {"album_art", 0, 0, 2, 2},
    {"track_info", 2, 0, 2, 2},
    {"media_controls", 0, 2, 3, 1},
    {"volume", 3, 2, 1, 1},
};

const Spec kSystem[] = {
    metric("metric_gauge", 0, 0, 1, 1, "sys.cpu.load", "CPU", "%", 0, 100, 80, 95),
    metric("metric_gauge", 1, 0, 1, 1, "sys.cpu.temp", "CPU \xC2\xB0" "C", "", 20, 100, 75, 90),
    metric("metric_gauge", 2, 0, 1, 1, "sys.gpu.load", "GPU", "%", 0, 100, 80, 95),
    metric("metric_gauge", 3, 0, 1, 1, "sys.gpu.temp", "GPU \xC2\xB0" "C", "", 20, 100, 75, 90),
    metric("metric_graph", 0, 1, 2, 1, "sys.cpu.load", "CPU history", "%", 0, 100, NAN, NAN),
    metric("metric_bar", 2, 1, 2, 1, "sys.ram.used_pct", "RAM", "%", 0, 100, 85, 95),
    metric("metric_value", 0, 2, 2, 1, "sys.net.down_kbps", "Down", "kB/s", 0, 0, NAN, NAN),
    metric("metric_value", 2, 2, 2, 1, "sys.net.up_kbps", "Up", "kB/s", 0, 0, NAN, NAN),
};

Spec forecast() {
  Spec s{"weather_forecast", 0, 2, 4, 1};
  s.mode = "daily";
  return s;
}

const Spec kWeatherRoom[] = {
    {"weather_now", 0, 0, 2, 2},
    {"room_climate", 2, 0, 2, 2},
    forecast(),
};

const PageSpec kPages[] = {
    {"now_playing", "Now Playing", kNowPlaying, sizeof(kNowPlaying) / sizeof(kNowPlaying[0])},
    {"system", "System", kSystem, sizeof(kSystem) / sizeof(kSystem[0])},
    {"weather_room", "Weather & Room", kWeatherRoom, sizeof(kWeatherRoom) / sizeof(kWeatherRoom[0])},
};

}  // namespace

const PageSpec* defaultPages(int& count) {
  count = sizeof(kPages) / sizeof(kPages[0]);
  return kPages;
}

}  // namespace ui
}  // namespace rp
