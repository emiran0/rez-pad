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

// What a press gesture does: a built-in action, a key shortcut sent by the device's USB keyboard
// (works without the host), or a macro the host runs (docs/config-schema.md, "Buttons and macros").
struct Binding {
  enum class Kind : uint8_t { kAction, kKeys, kMacro };
  Kind kind = Kind::kAction;
  Action action = Action::kNone;
  uint8_t mods = 0;   // kKeys: HID modifier bits (ctrl 0x01, shift 0x02, alt 0x04, win 0x08, altgr 0x40)
  uint8_t key = 0;    // kKeys: HID keyboard usage, 0 for modifiers only
  int8_t macro = -1;  // kMacro: index into Config::macroIds
  bool none() const { return kind == Kind::kAction && action == Action::kNone; }
};

constexpr int kMaxButtons = 4;

struct EncoderMap {
  Action rotate = Action::kNone;
  Action rotatePressed = Action::kNone;  // turned while the knob is held down
  Binding click, dbl, lng;
};

struct ButtonMap {
  Binding click, dbl, lng;
};

struct Map {
  EncoderMap enc[2];
  ButtonMap btn[kMaxButtons];
};

// "ctrl+shift+m", "f13", "win+altgr+e": modifiers and at most one key, names as in
// docs/config-schema.md. False for unknown names or an empty combination.
bool parseKeys(const char* s, uint8_t& mods, uint8_t& key);

}  // namespace actions

namespace cfg {

constexpr int kSchema = 1;
constexpr int kMaxPages = 8;
constexpr int kMaxWidgets = 16;  // per page
constexpr int kMaxGrid = 8;      // cols and rows
constexpr size_t kMaxConfigBytes = 32768;
constexpr int kMaxSources = 16;  // different data sources over all pages (SYS_STATS keeps 16)
constexpr int kMaxMacros = 32;   // the host runs them; the device only needs their ids

// Screen geometry the sizes are checked against (firmware/src/ui/pages.cpp uses the same numbers).
constexpr int kScreenW = 480;
constexpr int kGridH = 300;  // below the 20 px status bar
constexpr int kCellGap = 4;  // between cards
constexpr int kCardPad = 7;  // inside a card

// Theme presets (docs/config-schema.md, "Themes"), in this order.
constexpr int kThemeCount = 4;
const char* themeName(int index);  // "dark", "midnight", "black", "graphite"

// Card size in px for a widget of w x h cells on a cols x rows grid.
inline int cardW(int w, int cols) { return w * (kScreenW / cols) - kCellGap; }
inline int cardH(int h, int rows) { return h * (kGridH / rows) - kCellGap; }

enum class Fit : uint8_t { kCover, kContain };

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
  char asset[40] = ""; // image widget: asset id
  Fit fit = Fit::kCover;
  float min = 0, max = 100;
  float warn = NAN, crit = NAN;  // color thresholds (NAN = none)
  int8_t decimals = -1;          // -1 = automatic
};

struct Page {
  char id[24] = "";
  char name[32] = "";
  uint8_t cols = 4, rows = 3;  // this page's grid (the global grid unless the page sets one)
  uint8_t theme = 0;           // theme preset (the global one unless the page sets one)
  uint32_t accent = 0x4FC3F7;  // accent color (likewise)
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
  // grid (default for pages without their own)
  uint8_t cols = 4, rows = 3;
  // theme (default for pages without their own)
  uint8_t theme = 0;
  uint32_t accent = 0x4FC3F7;
  // units
  bool fahrenheit = false;
  uint8_t wind = 0;  // 0 km/h, 1 m/s, 2 mph
  bool inHg = false;
  // sensor
  float tempOffsetC = 0, humOffsetPct = 0, trendThresholdHpa = 1.0f;
  // media (the device moves its display optimistically by the same steps as the host)
  uint8_t volumeStep = 2, seekStepS = 5;
  // macros (ids only; the steps are the host's)
  uint8_t macroCount = 0;
  char macroIds[kMaxMacros][24] = {};
  // inputs
  uint8_t buttons = 0;  // 0 to actions::kMaxButtons fitted
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

// Widget types (docs/config-schema.md, "Widget catalog"). A widget may have any size whose
// content area (card minus padding) is at least the type's minimum.
bool knownType(const char* type);
bool minContent(const char* type, int& w, int& h);  // false for unknown types

// Unit, range and thresholds for a data source when the widget does not set them.
struct SourceStyle {
  const char* unit;
  float min, max;
  float warn, crit;
};
SourceStyle sourceDefaults(const char* source);

}  // namespace cfg
}  // namespace rp
