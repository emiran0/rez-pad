#include "config_logic.h"

#include <ArduinoJson.h>
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace rp {

namespace actions {

namespace {

struct Name {
  Action a;
  const char* s;
};
const Name kNames[] = {
    {Action::kNone, "none"},
    {Action::kNavPage, "nav.page"},
    {Action::kNavNext, "nav.next_page"},
    {Action::kNavPrev, "nav.prev_page"},
    {Action::kNavHome, "nav.home"},
    {Action::kPlayPause, "media.play_pause"},
    {Action::kNext, "media.next"},
    {Action::kPrev, "media.prev"},
    {Action::kSeek, "media.seek"},
    {Action::kVolume, "media.volume"},
    {Action::kMute, "media.mute"},
    {Action::kDisplayToggle, "display.toggle"},
    {Action::kDisplayBrightness, "display.brightness"},
    {Action::kAppVolume, "media.app_volume"},
    {Action::kAppMute, "media.app_mute"},
    {Action::kMixerFocus, "mixer.focus"},
};

}  // namespace

const char* name(Action a) {
  for (const auto& n : kNames) {
    if (n.a == a) return n.s;
  }
  return "none";
}

bool parse(const char* s, Action& out) {
  if (!s) return false;
  for (const auto& n : kNames) {
    if (strcmp(n.s, s) == 0) {
      out = n.a;
      return true;
    }
  }
  return false;
}

bool isRotate(Action a) {
  return a == Action::kNavPage || a == Action::kSeek || a == Action::kVolume || a == Action::kDisplayBrightness ||
         a == Action::kAppVolume;
}

namespace {

struct Key {
  const char* name;
  uint8_t usage;
};

// HID keyboard usages (USB HID Usage Tables, page 0x07). Letters and digits are generated.
const Key kKeys[] = {
    {"enter", 0x28}, {"esc", 0x29}, {"backspace", 0x2A}, {"tab", 0x2B}, {"space", 0x2C},
    {"minus", 0x2D}, {"equal", 0x2E}, {"lbracket", 0x2F}, {"rbracket", 0x30}, {"backslash", 0x31},
    {"semicolon", 0x33}, {"quote", 0x34}, {"grave", 0x35}, {"comma", 0x36}, {"period", 0x37},
    {"slash", 0x38}, {"capslock", 0x39}, {"printscreen", 0x46}, {"scrolllock", 0x47}, {"pause", 0x48},
    {"insert", 0x49}, {"home", 0x4A}, {"pgup", 0x4B}, {"delete", 0x4C}, {"end", 0x4D},
    {"pgdn", 0x4E}, {"right", 0x4F}, {"left", 0x50}, {"down", 0x51}, {"up", 0x52},
    {"numlock", 0x53}, {"numdiv", 0x54}, {"nummul", 0x55}, {"numsub", 0x56}, {"numadd", 0x57},
    {"numenter", 0x58}, {"numdot", 0x63}, {"intl", 0x64}, {"menu", 0x65},
};

struct Mod {
  const char* name;
  uint8_t bit;
};
const Mod kMods[] = {{"ctrl", 0x01}, {"shift", 0x02}, {"alt", 0x04}, {"win", 0x08}, {"altgr", 0x40}};

bool keyUsage(const char* n, uint8_t& usage) {
  const size_t len = strlen(n);
  if (len == 1 && n[0] >= 'a' && n[0] <= 'z') {
    usage = static_cast<uint8_t>(0x04 + (n[0] - 'a'));
    return true;
  }
  if (len == 1 && n[0] >= '1' && n[0] <= '9') {
    usage = static_cast<uint8_t>(0x1E + (n[0] - '1'));
    return true;
  }
  if (len == 1 && n[0] == '0') {
    usage = 0x27;
    return true;
  }
  if (n[0] == 'f' && len >= 2 && len <= 3) {
    const int f = atoi(n + 1);
    if (f >= 1 && f <= 12) usage = static_cast<uint8_t>(0x3A + f - 1);
    else if (f >= 13 && f <= 24) usage = static_cast<uint8_t>(0x68 + f - 13);
    else return false;
    return true;
  }
  if (len == 4 && strncmp(n, "num", 3) == 0 && n[3] >= '0' && n[3] <= '9') {
    usage = n[3] == '0' ? 0x62 : static_cast<uint8_t>(0x59 + (n[3] - '1'));
    return true;
  }
  for (const auto& k : kKeys) {
    if (strcmp(k.name, n) == 0) {
      usage = k.usage;
      return true;
    }
  }
  return false;
}

}  // namespace

bool parseKeys(const char* s, uint8_t& mods, uint8_t& key) {
  mods = 0;
  key = 0;
  if (!s || !s[0] || strlen(s) > 63) return false;
  char buf[64];
  size_t n = 0;
  for (const char* p = s;; ++p) {
    if (*p == '+' || *p == '\0') {
      buf[n] = '\0';
      if (n == 0) return false;  // "ctrl++", leading or trailing '+'
      bool isMod = false;
      for (const auto& m : kMods) {
        if (strcmp(m.name, buf) == 0) {
          mods |= m.bit;
          isMod = true;
        }
      }
      if (!isMod) {
        uint8_t u = 0;
        if (key != 0 || !keyUsage(buf, u)) return false;  // unknown, or a second key
        key = u;
      }
      n = 0;
      if (*p == '\0') break;
    } else {
      if (n >= sizeof(buf) - 1) return false;
      buf[n++] = static_cast<char>(*p >= 'A' && *p <= 'Z' ? *p - 'A' + 'a' : *p);
    }
  }
  return mods != 0 || key != 0;
}

}  // namespace actions

namespace cfg {

namespace {

using actions::Action;

// ---- widget catalog (docs/config-schema.md) ----

// Smallest content area (card minus padding) each type can draw in, in px.
struct TypeInfo {
  const char* type;
  uint16_t minW, minH;
};

const TypeInfo kTypes[] = {
    {"album_art", 40, 40},      {"track_info", 80, 18},   {"media_controls", 60, 24},
    {"progress", 60, 10},       {"volume", 40, 34},       {"now_playing", 150, 60},
    {"metric_value", 50, 30},   {"metric_bar", 60, 30},   {"metric_gauge", 44, 44},
    {"metric_graph", 80, 40},   {"clock", 50, 26},        {"room_climate", 60, 30},
    {"weather_now", 60, 34},    {"weather_forecast", 90, 46}, {"label", 16, 14},
    {"image", 16, 16},          {"spectrum", 60, 30},       {"server", 110, 60},
    {"mixer", 110, 50},
};

const char* const kThemes[kThemeCount] = {"dark", "midnight", "black", "graphite"};

const TypeInfo* findType(const char* type) {
  for (const auto& t : kTypes) {
    if (strcmp(t.type, type) == 0) return &t;
  }
  return nullptr;
}

bool isMetric(const char* type) { return strncmp(type, "metric_", 7) == 0; }

bool endsWith(const char* s, const char* suffix) {
  const size_t n = strlen(s), m = strlen(suffix);
  return n >= m && strcmp(s + n - m, suffix) == 0;
}

// ---- error reporting ----

class Ctx {
 public:
  Ctx(char* err, size_t n) : err_(err), n_(n) {
    if (err_ && n_) err_[0] = '\0';
  }

  bool fail(const char* path, const char* fmt, ...) {
    if (!err_ || !n_) return false;
    int used = snprintf(err_, n_, "%s: ", path);
    if (used < 0 || static_cast<size_t>(used) >= n_) return false;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err_ + used, n_ - used, fmt, ap);
    va_end(ap);
    return false;
  }

 private:
  char* err_;
  size_t n_;
};

// "parent.key" (or "key" at the root).
struct Path {
  char s[96];
  Path(const char* parent, const char* key) {
    if (parent && parent[0]) snprintf(s, sizeof(s), "%s.%s", parent, key);
    else snprintf(s, sizeof(s), "%s", key);
  }
};

// ---- typed getters: a missing or null key keeps the default ----

bool getObject(JsonObjectConst parent, const char* key, const char* path, JsonObjectConst& out, Ctx& c) {
  JsonVariantConst v = parent[key];
  if (v.isNull()) {
    out = JsonObjectConst();
    return true;
  }
  if (!v.is<JsonObjectConst>()) return c.fail(Path(path, key).s, "must be an object");
  out = v.as<JsonObjectConst>();
  return true;
}

template <typename T>
bool getInt(JsonObjectConst o, const char* key, const char* path, long lo, long hi, T& out, Ctx& c) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return true;
  if (!v.is<long>() || v.as<long>() < lo || v.as<long>() > hi) {
    return c.fail(Path(path, key).s, "must be a whole number from %ld to %ld", lo, hi);
  }
  out = static_cast<T>(v.as<long>());
  return true;
}

bool getFloat(JsonObjectConst o, const char* key, const char* path, float lo, float hi, float& out, Ctx& c) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return true;
  if (!v.is<float>() || v.as<float>() < lo || v.as<float>() > hi) {
    return c.fail(Path(path, key).s, "must be a number from %g to %g", lo, hi);
  }
  out = v.as<float>();
  return true;
}

// Optional number without range; `set` tells whether it was there.
bool getAnyFloat(JsonObjectConst o, const char* key, const char* path, float& out, bool& set, Ctx& c) {
  JsonVariantConst v = o[key];
  set = false;
  if (v.isNull()) return true;
  if (!v.is<float>()) return c.fail(Path(path, key).s, "must be a number");
  out = v.as<float>();
  set = true;
  return true;
}

bool getBool(JsonObjectConst o, const char* key, const char* path, bool& out, Ctx& c) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return true;
  if (!v.is<bool>()) return c.fail(Path(path, key).s, "must be true or false");
  out = v.as<bool>();
  return true;
}

// Strings are limited in UTF-8 bytes (cap - 1), like the host validator. `set` tells whether it
// was there.
bool getString(JsonObjectConst o, const char* key, const char* path, char* dst, size_t cap, bool& set, Ctx& c) {
  JsonVariantConst v = o[key];
  set = false;
  if (v.isNull()) return true;
  if (!v.is<const char*>()) return c.fail(Path(path, key).s, "must be a string");
  const char* s = v.as<const char*>();
  if (strlen(s) >= cap) return c.fail(Path(path, key).s, "too long (max %u bytes)", static_cast<unsigned>(cap - 1));
  strcpy(dst, s);
  set = true;
  return true;
}

bool getChoice(JsonObjectConst o, const char* key, const char* path, const char* const* options, uint8_t count,
               uint8_t& out, Ctx& c) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return true;
  const char* s = v.is<const char*>() ? v.as<const char*>() : nullptr;
  for (uint8_t i = 0; s && i < count; ++i) {
    if (strcmp(s, options[i]) == 0) {
      out = i;
      return true;
    }
  }
  char list[64] = "";
  for (uint8_t i = 0; i < count; ++i) {
    strncat(list, i ? ", " : "", sizeof(list) - strlen(list) - 1);
    strncat(list, options[i], sizeof(list) - strlen(list) - 1);
  }
  return c.fail(Path(path, key).s, "must be one of %s", list);
}

bool getTime(JsonObjectConst o, const char* key, const char* path, uint16_t& out, Ctx& c) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return true;
  const char* s = v.is<const char*>() ? v.as<const char*>() : "";
  const bool shape = strlen(s) == 5 && s[0] >= '0' && s[0] <= '9' && s[1] >= '0' && s[1] <= '9' && s[2] == ':' &&
                     s[3] >= '0' && s[3] <= '9' && s[4] >= '0' && s[4] <= '9';
  const int h = shape ? (s[0] - '0') * 10 + (s[1] - '0') : 99;
  const int m = shape ? (s[3] - '0') * 10 + (s[4] - '0') : 99;
  if (h > 23 || m > 59) return c.fail(Path(path, key).s, "must be a time \"HH:MM\"");
  out = static_cast<uint16_t>(h * 60 + m);
  return true;
}

bool getAction(JsonObjectConst o, const char* key, const char* path, bool rotate, Action& out, Ctx& c) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return true;
  Action a;
  if (!v.is<const char*>() || !actions::parse(v.as<const char*>(), a)) {
    return c.fail(Path(path, key).s, "unknown action");
  }
  if (a != Action::kNone && actions::isRotate(a) != rotate) {
    return c.fail(Path(path, key).s, rotate ? "%s needs a press, not a rotation" : "%s needs a rotation, not a press",
                  actions::name(a));
  }
  out = a;
  return true;
}

// A press gesture: an action name (not a rotate action), {"keys": "..."} or {"macro": "<id>"}.
bool getBinding(JsonObjectConst o, const char* key, const char* path, const Config& cfg, actions::Binding& out,
                Ctx& c) {
  JsonVariantConst v = o[key];
  if (v.isNull()) return true;
  actions::Binding b;
  if (v.is<const char*>()) {
    Action a;
    if (!actions::parse(v.as<const char*>(), a)) return c.fail(Path(path, key).s, "unknown action");
    if (a != Action::kNone && actions::isRotate(a)) {
      return c.fail(Path(path, key).s, "%s needs a rotation, not a press", actions::name(a));
    }
    b.action = a;
    out = b;
    return true;
  }
  if (!v.is<JsonObjectConst>()) return c.fail(Path(path, key).s, "must be an action name or {keys} or {macro}");
  JsonObjectConst ob = v.as<JsonObjectConst>();
  Path p(path, key);
  const bool hasKeys = !ob["keys"].isNull();
  const bool hasMacro = !ob["macro"].isNull();
  if (hasKeys == hasMacro) return c.fail(p.s, "needs exactly one of keys or macro");
  if (hasKeys) {
    const char* s = ob["keys"].is<const char*>() ? ob["keys"].as<const char*>() : "";
    if (!actions::parseKeys(s, b.mods, b.key)) return c.fail(Path(p.s, "keys").s, "unknown keys \"%s\"", s);
    b.kind = actions::Binding::Kind::kKeys;
  } else {
    const char* s = ob["macro"].is<const char*>() ? ob["macro"].as<const char*>() : "";
    for (int i = 0; i < cfg.macroCount && b.macro < 0; ++i) {
      if (strcmp(cfg.macroIds[i], s) == 0) b.macro = static_cast<int8_t>(i);
    }
    if (b.macro < 0) return c.fail(Path(p.s, "macro").s, "no macro \"%s\"", s);
    b.kind = actions::Binding::Kind::kMacro;
  }
  out = b;
  return true;
}

// ---- sections ----

bool parseDisplay(JsonObjectConst root, Config& cfg, Ctx& c) {
  JsonObjectConst d;
  if (!getObject(root, "display", "", d, c)) return false;
  if (d.isNull()) return true;
  if (!getInt(d, "brightness", "display", 0, 100, cfg.brightness, c)) return false;
  JsonObjectConst n;
  if (!getObject(d, "night", "display", n, c)) return false;
  if (!n.isNull()) {
    if (!getBool(n, "enabled", "display.night", cfg.nightEnabled, c)) return false;
    if (!getTime(n, "from", "display.night", cfg.nightFromMin, c)) return false;
    if (!getTime(n, "to", "display.night", cfg.nightToMin, c)) return false;
    if (!getInt(n, "brightness", "display.night", 0, 100, cfg.nightBrightness, c)) return false;
  }
  if (!getBool(d, "sleep_on_pc_sleep", "display", cfg.sleepOnPcSleep, c)) return false;
  return getInt(d, "spi_hz", "display", 1000000L, 80000000L, cfg.spiHz, c);
}

bool parseGrid(JsonObjectConst root, Config& cfg, Ctx& c) {
  JsonObjectConst g;
  if (!getObject(root, "grid", "", g, c)) return false;
  if (g.isNull()) return true;
  return getInt(g, "cols", "grid", 1, kMaxGrid, cfg.cols, c) && getInt(g, "rows", "grid", 1, kMaxGrid, cfg.rows, c);
}

// "theme" (global) or a page's "theme": preset name and accent color, each optional.
bool parseThemeObject(JsonObjectConst t, const char* path, uint8_t& theme, uint32_t& accent, Ctx& c) {
  if (!getChoice(t, "name", path, kThemes, kThemeCount, theme, c)) return false;
  JsonVariantConst a = t["accent"];
  if (a.isNull()) return true;
  const char* s = a.is<const char*>() ? a.as<const char*>() : "";
  bool ok = strlen(s) == 7 && s[0] == '#';
  uint32_t rgb = 0;
  for (int i = 1; ok && i < 7; ++i) {
    const char ch = s[i];
    int d = -1;
    if (ch >= '0' && ch <= '9') d = ch - '0';
    else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
    else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
    ok = d >= 0;
    rgb = (rgb << 4) | static_cast<uint32_t>(d < 0 ? 0 : d);
  }
  if (!ok) return c.fail(Path(path, "accent").s, "must be a color \"#RRGGBB\"");
  accent = rgb;
  return true;
}

bool parseTheme(JsonObjectConst root, Config& cfg, Ctx& c) {
  JsonObjectConst t;
  if (!getObject(root, "theme", "", t, c)) return false;
  if (t.isNull()) return true;
  return parseThemeObject(t, "theme", cfg.theme, cfg.accent, c);
}

bool parseUnits(JsonObjectConst root, Config& cfg, Ctx& c) {
  JsonObjectConst u;
  if (!getObject(root, "units", "", u, c)) return false;
  if (u.isNull()) return true;
  static const char* const kTemp[] = {"c", "f"};
  static const char* const kWind[] = {"kmh", "ms", "mph"};
  static const char* const kPress[] = {"hpa", "inhg"};
  uint8_t t = cfg.fahrenheit ? 1 : 0, p = cfg.inHg ? 1 : 0;
  if (!getChoice(u, "temp", "units", kTemp, 2, t, c)) return false;
  if (!getChoice(u, "wind", "units", kWind, 3, cfg.wind, c)) return false;
  if (!getChoice(u, "pressure", "units", kPress, 2, p, c)) return false;
  cfg.fahrenheit = t == 1;
  cfg.inHg = p == 1;
  return true;
}

bool parseSensor(JsonObjectConst root, Config& cfg, Ctx& c) {
  JsonObjectConst s;
  if (!getObject(root, "sensor", "", s, c)) return false;
  if (s.isNull()) return true;
  return getFloat(s, "temp_offset_c", "sensor", -10, 10, cfg.tempOffsetC, c) &&
         getFloat(s, "hum_offset_pct", "sensor", -20, 20, cfg.humOffsetPct, c) &&
         getFloat(s, "trend_threshold_hpa", "sensor", 0.1f, 10, cfg.trendThresholdHpa, c);
}

bool parseMedia(JsonObjectConst root, Config& cfg, Ctx& c) {
  JsonObjectConst m;
  if (!getObject(root, "media", "", m, c)) return false;
  if (m.isNull()) return true;
  return getInt(m, "volume_step", "media", 1, 20, cfg.volumeStep, c) &&
         getInt(m, "seek_step_s", "media", 1, 60, cfg.seekStepS, c);
}

// Gesture keys of enc1..enc3/btn1..btn4 (used by "inputs" and by a page's "inputs_override").
bool parseGestures(JsonObjectConst inputs, const char* path, const Config& cfg, actions::Map& map, Ctx& c) {
  static const char* const kEnc[] = {"enc1", "enc2", "enc3"};
  static const char* const kBtn[] = {"btn1", "btn2", "btn3", "btn4"};
  for (int i = 0; i < actions::kMaxKnobs; ++i) {
    JsonObjectConst e;
    if (!getObject(inputs, kEnc[i], path, e, c)) return false;
    if (e.isNull()) continue;
    Path p(path, kEnc[i]);
    actions::EncoderMap& m = map.enc[i];
    if (!getAction(e, "rotate", p.s, true, m.rotate, c) ||
        !getAction(e, "rotate_pressed", p.s, true, m.rotatePressed, c) ||
        !getBinding(e, "click", p.s, cfg, m.click, c) || !getBinding(e, "double", p.s, cfg, m.dbl, c) ||
        !getBinding(e, "long", p.s, cfg, m.lng, c)) {
      return false;
    }
  }
  for (int i = 0; i < actions::kMaxButtons; ++i) {
    JsonObjectConst b;
    if (!getObject(inputs, kBtn[i], path, b, c)) return false;
    if (b.isNull()) continue;
    Path p(path, kBtn[i]);
    if (!getBinding(b, "click", p.s, cfg, map.btn[i].click, c) || !getBinding(b, "double", p.s, cfg, map.btn[i].dbl, c) ||
        !getBinding(b, "long", p.s, cfg, map.btn[i].lng, c)) {
      return false;
    }
  }
  return true;
}

// "macros": the device keeps the ids, so buttons can name them; the steps are checked by the host.
bool parseMacros(JsonObjectConst root, Config& cfg, Ctx& c) {
  JsonVariantConst ms = root["macros"];
  if (ms.isNull()) return true;
  if (!ms.is<JsonArrayConst>()) return c.fail("macros", "must be an array");
  JsonArrayConst arr = ms.as<JsonArrayConst>();
  if (arr.size() > static_cast<size_t>(kMaxMacros)) return c.fail("macros", "at most %d macros", kMaxMacros);
  int i = 0;
  for (JsonVariantConst m : arr) {
    char path[16];
    snprintf(path, sizeof(path), "macros[%d]", i);
    if (!m.is<JsonObjectConst>()) return c.fail(path, "must be an object");
    bool set = false;
    char* id = cfg.macroIds[i];
    if (!getString(m.as<JsonObjectConst>(), "id", path, id, sizeof(cfg.macroIds[i]), set, c)) return false;
    if (!id[0]) return c.fail(Path(path, "id").s, "required, must not be empty");
    for (int k = 0; k < i; ++k) {
      if (strcmp(cfg.macroIds[k], id) == 0) return c.fail(Path(path, "id").s, "duplicate macro id \"%s\"", id);
    }
    ++i;
  }
  cfg.macroCount = static_cast<uint8_t>(i);
  return true;
}

// "knob_ports" / "button_ports": position = number - 1, value = physical port (1 to n), 0 = that
// number is not used. Ports in use must differ. Missing keeps the defaults; `set` tells whether it
// was there. Numbers after the array's end are not used.
bool getPorts(JsonObjectConst in, const char* key, int n, uint8_t* out, bool& set, Ctx& c) {
  JsonVariantConst v = in[key];
  set = false;
  if (v.isNull()) return true;
  const Path p("inputs", key);
  if (!v.is<JsonArrayConst>() || v.as<JsonArrayConst>().size() > static_cast<size_t>(n)) {
    return c.fail(p.s, "must be an array of up to %d port numbers", n);
  }
  uint8_t ports[actions::kMaxButtons > actions::kMaxKnobs ? actions::kMaxButtons : actions::kMaxKnobs] = {};
  int i = 0;
  for (JsonVariantConst e : v.as<JsonArrayConst>()) {
    if (!e.is<long>() || e.as<long>() < 0 || e.as<long>() > n) {
      return c.fail(p.s, "port numbers are 0 (not used) to %d", n);
    }
    ports[i] = static_cast<uint8_t>(e.as<long>());
    for (int k = 0; k < i; ++k) {
      if (ports[i] && ports[k] == ports[i]) return c.fail(p.s, "port %u is used twice", ports[i]);
    }
    ++i;
  }
  for (int k = 0; k < n; ++k) out[k] = ports[k];
  set = true;
  return true;
}

bool parseInputs(JsonObjectConst root, Config& cfg, Ctx& c) {
  JsonObjectConst in;
  if (!getObject(root, "inputs", "", in, c)) return false;
  if (in.isNull()) return true;
  uint8_t legacyButtons = 0;
  if (!getInt(in, "buttons", "inputs", 0, actions::kMaxButtons, legacyButtons, c)) return false;
  // "button_ports" replaces the older "buttons" count (which meant ports 1 to n).
  bool set = false;
  if (!getPorts(in, "knob_ports", actions::kMaxKnobs, cfg.knobPorts, set, c)) return false;
  if (!getPorts(in, "button_ports", actions::kMaxButtons, cfg.buttonPorts, set, c)) return false;
  if (!set) {
    for (int i = 0; i < actions::kMaxButtons; ++i) cfg.buttonPorts[i] = i < legacyButtons ? i + 1 : 0;
  }
  cfg.buttons = 0;
  for (uint8_t port : cfg.buttonPorts) cfg.buttons += port ? 1 : 0;
  static const char* const kEnc[] = {"enc1", "enc2", "enc3"};
  for (int i = 0; i < actions::kMaxKnobs; ++i) {
    JsonObjectConst e;
    if (!getObject(in, kEnc[i], "inputs", e, c)) return false;
    if (e.isNull()) continue;
    Path p("inputs", kEnc[i]);
    if (!getInt(e, "counts_per_detent", p.s, 1, 8, cfg.countsPerDetent[i], c)) return false;
    if (!getBool(e, "invert", p.s, cfg.invert[i], c)) return false;
  }
  return parseGestures(in, "inputs", cfg, cfg.inputs, c);
}

bool parseWidget(JsonVariantConst v, const char* path, const Page& page, bool used[kMaxGrid][kMaxGrid],
                 WidgetSpec& w, Ctx& c) {
  if (!v.is<JsonObjectConst>()) return c.fail(path, "must be an object");
  JsonObjectConst o = v.as<JsonObjectConst>();
  bool set = false;
  if (!getString(o, "type", path, w.type, sizeof(w.type), set, c)) return false;
  const TypeInfo* info = set ? findType(w.type) : nullptr;
  if (!info) return c.fail(path, "unknown widget type \"%s\"", set ? w.type : "");

  long x = -1, y = -1, ww = 0, hh = 0;
  if (!getInt(o, "x", path, 0, kMaxGrid - 1, x, c) || !getInt(o, "y", path, 0, kMaxGrid - 1, y, c) ||
      !getInt(o, "w", path, 1, kMaxGrid, ww, c) || !getInt(o, "h", path, 1, kMaxGrid, hh, c)) {
    return false;
  }
  if (x < 0 || y < 0 || ww == 0 || hh == 0) return c.fail(path, "x, y, w and h are required");
  if (x + ww > page.cols || y + hh > page.rows) {
    return c.fail(path, "outside the %ux%u grid", page.cols, page.rows);
  }
  const int contentW = cardW(ww, page.cols) - 2 * kCardPad;
  const int contentH = cardH(hh, page.rows) - 2 * kCardPad;
  if (contentW < info->minW || contentH < info->minH) {
    return c.fail(path, "%ldx%ld is too small for %s on a %ux%u grid (needs %ux%u px, has %dx%d)", ww, hh, w.type,
                  page.cols, page.rows, info->minW, info->minH, contentW, contentH);
  }
  for (long cy = y; cy < y + hh; ++cy) {
    for (long cx = x; cx < x + ww; ++cx) {
      if (used[cy][cx]) return c.fail(path, "overlaps another widget");
    }
  }
  for (long cy = y; cy < y + hh; ++cy)
    for (long cx = x; cx < x + ww; ++cx) used[cy][cx] = true;
  w.x = static_cast<uint8_t>(x);
  w.y = static_cast<uint8_t>(y);
  w.w = static_cast<uint8_t>(ww);
  w.h = static_cast<uint8_t>(hh);

  if (!getString(o, "source", path, w.source, sizeof(w.source), set, c)) return false;
  if (isMetric(w.type) && !w.source[0]) return c.fail(Path(path, "source").s, "required for %s", w.type);
  if (strcmp(w.type, "server") == 0) {
    // "system": the Beszel machine's slug (lowercase letters, digits, '-', up to 14).
    if (!getString(o, "system", path, w.source, 15, set, c)) return false;
    bool ok = w.source[0] != '\0';
    for (const char* q = w.source; *q && ok; ++q) ok = (*q >= 'a' && *q <= 'z') || (*q >= '0' && *q <= '9') || *q == '-';
    if (!ok) return c.fail(Path(path, "system").s, "required, a Beszel system id (a-z, 0-9, -)");
  }
  if (!getString(o, "label", path, w.label, sizeof(w.label), set, c)) return false;
  bool unitSet = false;
  if (!getString(o, "unit", path, w.unit, sizeof(w.unit), unitSet, c)) return false;
  if (strcmp(w.type, "weather_forecast") == 0) {
    static const char* const kModes[] = {"daily", "hourly"};
    uint8_t mode = 0;
    if (!getChoice(o, "mode", path, kModes, 2, mode, c)) return false;
    strcpy(w.mode, kModes[mode]);
  }
  if (!getString(o, "text", path, w.text, sizeof(w.text), set, c)) return false;
  if (strcmp(w.type, "label") == 0 && !w.text[0]) return c.fail(Path(path, "text").s, "required for label");
  if (!getString(o, "asset", path, w.asset, sizeof(w.asset), set, c)) return false;
  if (strcmp(w.type, "image") == 0) {
    if (!w.asset[0]) return c.fail(Path(path, "asset").s, "required for image");
    static const char* const kFits[] = {"cover", "contain"};
    uint8_t fit = 0;
    if (!getChoice(o, "fit", path, kFits, 2, fit, c)) return false;
    w.fit = static_cast<Fit>(fit);
  }
  if (strcmp(w.type, "spectrum") == 0 && !getInt(o, "bars", path, 4, 32, w.bars, c)) return false;
  if (strcmp(w.type, "mixer") == 0) {
    static const char* const kLayouts[] = {"list", "faders"};
    uint8_t layout = 0;
    if (!getChoice(o, "layout", path, kLayouts, 2, layout, c)) return false;
    w.faders = layout == 1;
    // "apps": ids (lowercase process names, from MIXER) shown first, in this order.
    JsonVariantConst apps = o["apps"];
    if (!apps.isNull()) {
      if (!apps.is<JsonArrayConst>()) return c.fail(Path(path, "apps").s, "must be an array");
      JsonArrayConst arr = apps.as<JsonArrayConst>();
      if (arr.size() > 8) return c.fail(Path(path, "apps").s, "at most 8 apps");
      int k = 0;
      for (JsonVariantConst a : arr) {
        char p[56];
        snprintf(p, sizeof(p), "%s.apps[%d]", path, k);
        const char* id = a.is<const char*>() ? a.as<const char*>() : nullptr;
        const size_t n = id ? strlen(id) : 0;
        if (!id || n == 0 || n > 23 || strchr(id, ',')) return c.fail(p, "must be an app id (1 to 23 bytes, no comma)");
        const size_t used = strlen(w.apps);
        if (used + (used ? 1 : 0) + n >= sizeof(w.apps)) return c.fail(Path(path, "apps").s, "too long (63 bytes in all)");
        if (used) strcat(w.apps, ",");
        strcat(w.apps, id);
        ++k;
      }
    }
    if (!getBool(o, "only", path, w.onlyApps, c)) return false;
  }
  if (strcmp(w.type, "volume") == 0) {
    static const char* const kTargets[] = {"master", "app"};
    uint8_t target = 0;
    if (!getChoice(o, "target", path, kTargets, 2, target, c)) return false;
    w.appTarget = target == 1;
  }

  const SourceStyle def = sourceDefaults(w.source);
  if (!unitSet) strcpy(w.unit, def.unit);
  w.min = def.min;
  w.max = def.max;
  w.warn = def.warn;
  w.crit = def.crit;
  bool any = false;
  if (!getAnyFloat(o, "min", path, w.min, any, c) || !getAnyFloat(o, "max", path, w.max, any, c) ||
      !getAnyFloat(o, "warn", path, w.warn, any, c) || !getAnyFloat(o, "crit", path, w.crit, any, c) ||
      !getInt(o, "decimals", path, 0, 3, w.decimals, c)) {
    return false;
  }
  if (!(w.min < w.max)) return c.fail(path, "min must be below max");
  return true;
}

bool parsePage(JsonVariantConst v, int index, Config& cfg, Ctx& c) {
  char path[24];
  snprintf(path, sizeof(path), "pages[%d]", index);
  if (!v.is<JsonObjectConst>()) return c.fail(path, "must be an object");
  JsonObjectConst o = v.as<JsonObjectConst>();
  Page& page = cfg.pages[index];
  bool set = false;
  if (!getString(o, "id", path, page.id, sizeof(page.id), set, c)) return false;
  if (!page.id[0]) return c.fail(Path(path, "id").s, "required, must not be empty");
  for (int i = 0; i < index; ++i) {
    if (strcmp(cfg.pages[i].id, page.id) == 0) return c.fail(Path(path, "id").s, "duplicate page id \"%s\"", page.id);
  }
  if (!getString(o, "name", path, page.name, sizeof(page.name), set, c)) return false;
  if (!page.name[0]) strcpy(page.name, page.id);

  // Own grid and theme, falling back to the global ones.
  page.cols = cfg.cols;
  page.rows = cfg.rows;
  JsonObjectConst g;
  if (!getObject(o, "grid", path, g, c)) return false;
  if (!g.isNull()) {
    Path gp(path, "grid");
    if (!getInt(g, "cols", gp.s, 1, kMaxGrid, page.cols, c) || !getInt(g, "rows", gp.s, 1, kMaxGrid, page.rows, c)) {
      return false;
    }
  }
  page.theme = cfg.theme;
  page.accent = cfg.accent;
  JsonObjectConst t;
  if (!getObject(o, "theme", path, t, c)) return false;
  if (!t.isNull()) {
    Path tp(path, "theme");
    if (!parseThemeObject(t, tp.s, page.theme, page.accent, c)) return false;
  }

  JsonVariantConst ws = o["widgets"];
  if (!ws.isNull()) {
    if (!ws.is<JsonArrayConst>()) return c.fail(Path(path, "widgets").s, "must be an array");
    JsonArrayConst arr = ws.as<JsonArrayConst>();
    if (arr.size() > static_cast<size_t>(kMaxWidgets)) {
      return c.fail(Path(path, "widgets").s, "at most %d widgets per page", kMaxWidgets);
    }
    bool used[kMaxGrid][kMaxGrid] = {};
    int i = 0;
    for (JsonVariantConst w : arr) {
      char wpath[40];
      snprintf(wpath, sizeof(wpath), "%s.widgets[%d]", path, i);
      if (!parseWidget(w, wpath, page, used, page.widgets[i], c)) return false;
      ++i;
    }
    page.widgetCount = static_cast<uint8_t>(i);
  }

  page.inputs = cfg.inputs;
  JsonObjectConst ov;
  if (!getObject(o, "inputs_override", path, ov, c)) return false;
  if (!ov.isNull()) {
    Path p(path, "inputs_override");
    if (!parseGestures(ov, p.s, cfg, page.inputs, c)) return false;
  }
  return true;
}

bool parseStatusBar(JsonObjectConst root, Config& cfg, Ctx& c) {
  JsonObjectConst sb;
  if (!getObject(root, "status_bar", "", sb, c)) return false;
  if (sb.isNull()) return true;
  JsonVariantConst items = sb["items"];
  if (items.isNull()) return true;
  if (!items.is<JsonArrayConst>()) return c.fail("status_bar.items", "must be an array");
  JsonArrayConst arr = items.as<JsonArrayConst>();
  if (arr.size() > static_cast<size_t>(kMaxStatusItems)) {
    return c.fail("status_bar.items", "at most %d items", kMaxStatusItems);
  }
  static const char* const kTypes[] = {"date", "room_temp", "room_hum", "outdoor_temp", "volume", "metric"};
  int i = 0;
  for (JsonVariantConst v : arr) {
    char path[32];
    snprintf(path, sizeof(path), "status_bar.items[%d]", i);
    if (!v.is<JsonObjectConst>()) return c.fail(path, "must be an object");
    JsonObjectConst o = v.as<JsonObjectConst>();
    StatusItem& it = cfg.status[i];
    if (o["type"].isNull()) return c.fail(Path(path, "type").s, "required");
    uint8_t type = 0;
    if (!getChoice(o, "type", path, kTypes, 6, type, c)) return false;
    it.type = static_cast<StatusType>(type);
    bool set = false;
    if (!getString(o, "source", path, it.source, sizeof(it.source), set, c)) return false;
    if (it.type == StatusType::kMetric && !it.source[0]) return c.fail(Path(path, "source").s, "required for metric");
    if (!getString(o, "label", path, it.label, sizeof(it.label), set, c)) return false;
    ++i;
  }
  cfg.statusCount = static_cast<uint8_t>(i);
  return true;
}

bool parsePages(JsonObjectConst root, Config& cfg, Ctx& c) {
  JsonVariantConst ps = root["pages"];
  if (!ps.is<JsonArrayConst>()) return c.fail("pages", "required, must be an array");
  JsonArrayConst arr = ps.as<JsonArrayConst>();
  if (arr.size() < 1 || arr.size() > static_cast<size_t>(kMaxPages)) {
    return c.fail("pages", "must have 1 to %d pages", kMaxPages);
  }
  int i = 0;
  for (JsonVariantConst p : arr) {
    if (!parsePage(p, i, cfg, c)) return false;
    ++i;
  }
  cfg.pageCount = static_cast<uint8_t>(i);

  // Different data sources over the whole config: metric widgets, server cards (8 each) and
  // status bar items. Static: too big for a task stack; parse() is never run concurrently.
  static char seen[kMaxSources][32];
  int n = 0;
  auto add = [&](const char* id) {
    for (int k = 0; k < n; ++k) {
      if (strcmp(seen[k], id) == 0) return true;
    }
    if (n == kMaxSources) return false;
    snprintf(seen[n++], sizeof(seen[0]), "%s", id);
    return true;
  };
  for (int p = 0; p < cfg.pageCount; ++p) {
    for (int w = 0; w < cfg.pages[p].widgetCount; ++w) {
      const WidgetSpec& ws = cfg.pages[p].widgets[w];
      bool ok = true;
      if (isMetric(ws.type)) {
        ok = add(ws.source);
      } else if (strcmp(ws.type, "server") == 0) {
        for (int m = 0; m < kServerMetricCount && ok; ++m) {
          char id[32];
          snprintf(id, sizeof(id), "bz.%s.%s", ws.source, kServerMetrics[m]);
          ok = add(id);
        }
      }
      if (!ok) return c.fail("pages", "at most %d different data sources", kMaxSources);
    }
  }
  for (int s = 0; s < cfg.statusCount; ++s) {
    const StatusItem& it = cfg.status[s];
    if (it.type == StatusType::kMetric && !add(it.source)) {
      return c.fail("pages", "at most %d different data sources", kMaxSources);
    }
  }
  return true;
}

}  // namespace

const char* const kServerMetrics[kServerMetricCount] = {
    "online", "cpu_load", "mem_used_pct", "disk_used_pct", "load1", "temp", "net_rx_kbps", "net_tx_kbps",
};

bool knownType(const char* type) { return findType(type) != nullptr; }

bool minContent(const char* type, int& w, int& h) {
  const TypeInfo* t = findType(type);
  if (!t) return false;
  w = t->minW;
  h = t->minH;
  return true;
}

const char* themeName(int index) { return index >= 0 && index < kThemeCount ? kThemes[index] : "dark"; }

SourceStyle sourceDefaults(const char* s) {
  static const char kDeg[] = "\xC2\xB0" "C";
  if (!s || !s[0]) return {"", 0, 100, NAN, NAN};
  if (strstr(s, ".temp") || strstr(s, "_temp")) return {kDeg, 20, 100, 75, 90};
  if (endsWith(s, ".load") || endsWith(s, "_load")) return {"%", 0, 100, 80, 95};
  if (endsWith(s, "used_pct") || endsWith(s, "vram_pct")) {
    return strncmp(s, "sys.disk.", 9) == 0 ? SourceStyle{"%", 0, 100, 90, 97} : SourceStyle{"%", 0, 100, 85, 95};
  }
  if (endsWith(s, "_pct") || strcmp(s, "room.hum") == 0) return {"%", 0, 100, NAN, NAN};
  if (endsWith(s, "_kbps")) return {"kB/s", 0, 10000, NAN, NAN};
  if (endsWith(s, "_mhz")) return {"MHz", 0, 6000, NAN, NAN};
  if (endsWith(s, "_w")) return {"W", 0, 300, NAN, NAN};
  if (endsWith(s, "_gb")) return {"GB", 0, 64, NAN, NAN};
  if (endsWith(s, ".rpm") || endsWith(s, "_rpm")) return {"rpm", 0, 3000, NAN, NAN};
  if (strcmp(s, "room.pressure") == 0) return {"hPa", 950, 1050, NAN, NAN};
  return {"", 0, 100, NAN, NAN};
}

bool parse(const char* json, size_t len, Config& out, char* err, size_t errLen) {
  Ctx c(err, errLen);
  // Reset in place: a Config is about 25 KB, too big for a temporary on a task stack.
  out.~Config();
  new (&out) Config();
  if (len > kMaxConfigBytes) return c.fail("config", "too large (max %u bytes)", static_cast<unsigned>(kMaxConfigBytes));
  JsonDocument doc;
  const DeserializationError e = deserializeJson(doc, json, len);
  if (e) return c.fail("config", "invalid JSON (%s)", e.c_str());
  if (!doc.is<JsonObjectConst>()) return c.fail("config", "must be a JSON object");
  JsonObjectConst root = doc.as<JsonObjectConst>();

  JsonVariantConst schema = root["schema"];
  if (!schema.is<long>() || schema.as<long>() != kSchema) return c.fail("schema", "must be %d", kSchema);

  return parseDisplay(root, out, c) && parseGrid(root, out, c) && parseTheme(root, out, c) &&
         parseUnits(root, out, c) && parseSensor(root, out, c) && parseMedia(root, out, c) &&
         parseMacros(root, out, c) && parseInputs(root, out, c) && parseStatusBar(root, out, c) &&
         parsePages(root, out, c);
}

}  // namespace cfg
}  // namespace rp
