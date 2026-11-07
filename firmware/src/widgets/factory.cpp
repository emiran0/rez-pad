#include <string.h>

#include "media_widgets.h"
#include "metric_widgets.h"
#include "weather_widgets.h"
#include "widget.h"

namespace rp {
namespace widgets {

namespace {

struct Size {
  uint8_t w, h;
};

struct TypeInfo {
  const char* type;
  Size sizes[4];
  uint8_t count;  // 0 = any size
};

// docs/config-schema.md, "Widget catalog".
const TypeInfo kTypes[] = {
    {"album_art", {{2, 2}}, 1},
    {"track_info", {{2, 1}, {2, 2}, {4, 1}}, 3},
    {"media_controls", {{2, 1}, {3, 1}, {4, 1}}, 3},
    {"progress", {{2, 1}, {4, 1}}, 2},
    {"volume", {{1, 1}, {2, 1}}, 2},
    {"metric_value", {{1, 1}, {2, 1}}, 2},
    {"metric_bar", {{2, 1}, {4, 1}}, 2},
    {"metric_gauge", {{1, 1}, {2, 2}}, 2},
    {"metric_graph", {{2, 1}, {2, 2}, {4, 1}}, 3},
    {"clock", {{1, 1}, {2, 1}}, 2},
    {"room_climate", {{2, 1}, {2, 2}}, 2},
    {"weather_now", {{2, 1}, {2, 2}}, 2},
    {"weather_forecast", {{4, 1}}, 1},
    {"label", {}, 0},
    {"spectrum", {{4, 1}}, 1},
};

}  // namespace

bool sizeAllowed(const char* type, int w, int h) {
  for (const auto& t : kTypes) {
    if (strcmp(t.type, type) != 0) continue;
    if (t.count == 0) return w > 0 && h > 0;
    for (uint8_t i = 0; i < t.count; ++i) {
      if (t.sizes[i].w == w && t.sizes[i].h == h) return true;
    }
    return false;
  }
  return false;
}

Widget* create(const Spec& s) {
  const char* t = s.type;
  if (strcmp(t, "album_art") == 0) return new AlbumArt(s);
  if (strcmp(t, "track_info") == 0) return new TrackInfo(s);
  if (strcmp(t, "media_controls") == 0) return new MediaControls(s, true);
  if (strcmp(t, "progress") == 0) return new MediaControls(s, false);
  if (strcmp(t, "volume") == 0) return new VolumeWidget(s);
  if (strcmp(t, "metric_value") == 0) return new MetricValue(s);
  if (strcmp(t, "metric_bar") == 0) return new MetricBar(s);
  if (strcmp(t, "metric_gauge") == 0) return new MetricGauge(s);
  if (strcmp(t, "metric_graph") == 0) return new MetricGraph(s);
  if (strcmp(t, "weather_now") == 0) return new WeatherNow(s);
  if (strcmp(t, "weather_forecast") == 0) return new WeatherForecast(s);
  if (strcmp(t, "room_climate") == 0) return new RoomClimate(s);
  return nullptr;  // clock, label, spectrum: later phases
}

}  // namespace widgets
}  // namespace rp
