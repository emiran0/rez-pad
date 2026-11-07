// Device config (docs/config-schema.md, schema 1): parsing and validation into a fixed-size struct.
// Pure C++ (ArduinoJson only), unit-tested on the host against docs/config-test-vectors.json, the
// same cases the host's validator is tested with. No allocation besides ArduinoJson's while parsing.
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

namespace rp {

// Input actions (docs/config-schema.md, "Actions").
namespace actions {

enum class Action : uint8_t {
  kNone,
  kNavPage,  // rotate
  kNavNext,
  kNavPrev,
  kNavHome,
  kPlayPause,
  kNext,
  kPrev,
  kSeek,    // rotate
  kVolume,  // rotate
  kMute,
  kDisplayToggle,
  kDisplayBrightness,  // rotate
};

const char* name(Action a);
// False for unknown names.
bool parse(const char* s, Action& out);
// Actions that take a detent count (allowed on rotate gestures only; the others on presses only).
bool isRotate(Action a);

struct EncoderMap {
  Action rotate = Action::kNone;
  Action rotatePressed = Action::kNone;  // turned while the knob is held down
  Action click = Action::kNone;
  Action dbl = Action::kNone;
  Action lng = Action::kNone;
};

struct ButtonMap {
  Action click = Action::kNone;
  Action lng = Action::kNone;
};

struct Map {
  EncoderMap enc[2];
  ButtonMap btn[2];
};

}  // namespace actions

namespace cfg {

constexpr int kSchema = 1;
constexpr int kMaxPages = 8;
constexpr int kMaxWidgets = 16;  // per page
constexpr int kMaxGrid = 8;      // cols and rows
constexpr size_t kMaxConfigBytes = 32768;
constexpr int kMaxSources = 16;  // different data sources over all pages (SYS_STATS keeps 16)

// One widget on a page (a "widgets" entry). Strings are empty when not set; style fields that
// are not set get defaults from the data source (sourceDefaults) after parsing.
struct WidgetSpec {
  char type[20] = "";
  uint8_t x = 0, y = 0, w = 0, h = 0;  // grid cell position and size
  char source[40] = "";  // metric_*: data source id
  char label[32] = "";
  char unit[12] = "";
  char mode[8] = "";   // weather_forecast: "daily" or "hourly"
  char text[64] = "";  // label widget
  float min = 0, max = 100;
  float warn = NAN, crit = NAN;  // color thresholds (NAN = none)
  int8_t decimals = -1;          // -1 = automatic
};

struct Page {
  char id[24] = "";
  char name[32] = "";
  uint8_t widgetCount = 0;
  WidgetSpec widgets[kMaxWidgets];
  actions::Map inputs;  // global inputs with this page's inputs_override applied
};

struct Config {
  // display
  uint8_t brightness = 80;
  bool nightEnabled = false;
  uint16_t nightFromMin = 23 * 60, nightToMin = 7 * 60 + 30;  // minutes since midnight
  uint8_t nightBrightness = 15;
  bool sleepOnPcSleep = true;
  uint32_t spiHz = 40000000;
  // grid
  uint8_t cols = 4, rows = 3;
  // theme
  uint32_t accent = 0x4FC3F7;
  // units
  bool fahrenheit = false;
  uint8_t wind = 0;  // 0 km/h, 1 m/s, 2 mph
  bool inHg = false;
  // sensor
  float tempOffsetC = 0, humOffsetPct = 0, trendThresholdHpa = 1.0f;
  // media (the device moves its display optimistically by the same steps as the host)
  uint8_t volumeStep = 2, seekStepS = 5;
  // inputs
  uint8_t buttons = 0;
  uint8_t countsPerDetent[2] = {4, 4};
  bool invert[2] = {false, false};
  actions::Map inputs;
  // pages
  uint8_t pageCount = 0;
  Page pages[kMaxPages];
};

// Parses and validates `json`. On failure returns false and writes "<path>: <reason>" to `err`
// (path like "pages[1].widgets[2].source", matching the host's validator); `out` is then undefined.
bool parse(const char* json, size_t len, Config& out, char* err, size_t errLen);

// Allowed sizes per widget type (docs/config-schema.md, "Widget catalog"). False for unknown types.
bool knownType(const char* type);
bool sizeAllowed(const char* type, int w, int h);

// Unit, range and thresholds for a data source when the widget does not set them.
struct SourceStyle {
  const char* unit;
  float min, max;
  float warn, crit;
};
SourceStyle sourceDefaults(const char* source);

}  // namespace cfg
}  // namespace rp
