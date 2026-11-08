#include <string.h>

#include "media_widgets.h"
#include "metric_widgets.h"
#include "image_widget.h"
#include "mixer_widget.h"
#include "server_widget.h"
#include "spectrum_widget.h"
#include "text_widgets.h"
#include "weather_widgets.h"
#include "widget.h"

namespace rp {
namespace widgets {

Widget* create(const Spec& s) {
  const char* t = s.type;
  if (strcmp(t, "album_art") == 0) return new AlbumArt(s);
  if (strcmp(t, "track_info") == 0) return new TrackInfo(s);
  if (strcmp(t, "media_controls") == 0) return new MediaControls(s, true);
  if (strcmp(t, "progress") == 0) return new MediaControls(s, false);
  if (strcmp(t, "now_playing") == 0) return new NowPlayingWidget(s);
  if (strcmp(t, "volume") == 0) return new VolumeWidget(s);
  if (strcmp(t, "metric_value") == 0) return new MetricValue(s);
  if (strcmp(t, "metric_bar") == 0) return new MetricBar(s);
  if (strcmp(t, "metric_gauge") == 0) return new MetricGauge(s);
  if (strcmp(t, "metric_graph") == 0) return new MetricGraph(s);
  if (strcmp(t, "weather_now") == 0) return new WeatherNow(s);
  if (strcmp(t, "weather_forecast") == 0) return new WeatherForecast(s);
  if (strcmp(t, "room_climate") == 0) return new RoomClimate(s);
  if (strcmp(t, "clock") == 0) return new Clock(s);
  if (strcmp(t, "label") == 0) return new Label(s);
  if (strcmp(t, "image") == 0) return new ImageWidget(s);
  if (strcmp(t, "spectrum") == 0) return new SpectrumWidget(s);
  if (strcmp(t, "mixer") == 0) return new MixerWidget(s);
  if (strcmp(t, "server") == 0) return new ServerWidget(s);
  return nullptr;
}

}  // namespace widgets
}  // namespace rp
