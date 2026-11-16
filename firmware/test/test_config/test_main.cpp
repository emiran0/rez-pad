// Config parsing and validation tests against docs/config-test-vectors.json (shared with the host).
// Run: pio test -e native
#include <ArduinoJson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "config_logic.h"
#include "default_config.h"
#include "vectors.h"

using rp::cfg::Config;
using rp::actions::Action;

void setUp() {}
void tearDown() {}

static Config* gCfg = new Config();  // about 25 KB, keep it off the stack
static char gErr[160];

static bool parseText(const char* json) { return rp::cfg::parse(json, strlen(json), *gCfg, gErr, sizeof(gErr)); }

// JSON pointer ("/pages/0/widgets/-") to the parent container and the last token.
static JsonVariant parentOf(JsonDocument& doc, const char* pointer, char* last, size_t lastLen) {
  JsonVariant cur = doc.as<JsonVariant>();
  const char* p = pointer + 1;
  for (;;) {
    const char* slash = strchr(p, '/');
    const size_t n = slash ? static_cast<size_t>(slash - p) : strlen(p);
    char tok[64];
    snprintf(tok, sizeof(tok), "%.*s", static_cast<int>(n), p);
    if (!slash) {
      snprintf(last, lastLen, "%s", tok);
      return cur;
    }
    if (cur.is<JsonArray>()) cur = cur.as<JsonArray>()[atoi(tok)];
    else cur = cur.as<JsonObject>()[tok];
    p = slash + 1;
  }
}

static void applyPatch(JsonDocument& doc, JsonArrayConst patch) {
  for (JsonObjectConst op : patch) {
    char last[64];
    JsonVariant parent = parentOf(doc, op["path"].as<const char*>(), last, sizeof(last));
    const bool remove = strcmp(op["op"].as<const char*>(), "remove") == 0;
    if (parent.is<JsonArray>()) {
      JsonArray arr = parent.as<JsonArray>();
      if (remove) arr.remove(atoi(last));
      else if (strcmp(last, "-") == 0) arr.add(op["value"]);
      else arr[atoi(last)].set(op["value"]);
    } else {
      JsonObject obj = parent.as<JsonObject>();
      if (remove) obj.remove(last);
      else obj[last].set(op["value"]);
    }
  }
}

void test_default_config() {
  TEST_ASSERT_TRUE_MESSAGE(parseText(rp::cfg::kDefaultConfigJson), gErr);
  const Config& c = *gCfg;
  TEST_ASSERT_EQUAL(80, c.brightness);
  TEST_ASSERT_EQUAL(4, c.cols);
  TEST_ASSERT_EQUAL(3, c.rows);
  TEST_ASSERT_EQUAL(23 * 60, c.nightFromMin);
  TEST_ASSERT_EQUAL(7 * 60 + 30, c.nightToMin);
  TEST_ASSERT_EQUAL_HEX32(0x4FC3F7, c.accent);
  TEST_ASSERT_EQUAL_FLOAT(-1.5f, c.tempOffsetC);
  TEST_ASSERT_EQUAL(4, c.countsPerDetent[0]);
  TEST_ASSERT_EQUAL(3, c.pageCount);
  TEST_ASSERT_EQUAL_STRING("Weather & Room", c.pages[2].name);
  TEST_ASSERT_EQUAL(8, c.pages[1].widgetCount);
  TEST_ASSERT_TRUE(c.inputs.enc[0].rotate == Action::kNavPage);
  TEST_ASSERT_TRUE(c.inputs.enc[1].dbl.action == Action::kNext);
  TEST_ASSERT_TRUE(c.pages[0].inputs.enc[1].click.action == Action::kPlayPause);  // no override: global map
  TEST_ASSERT_EQUAL_STRING("daily", c.pages[2].widgets[2].mode);
}

// Every panel in every orientation has a built-in default that is valid on that screen.
void test_default_layouts_fit_every_screen() {
  char err[160];
  for (int p = 0; p < rp::cfg::kPanelCount; ++p) {
    for (int o = 0; o < rp::cfg::kOrientationCount; ++o) {
      TEST_ASSERT_TRUE_MESSAGE(rp::cfg::parseDefault(p, o, *gCfg, err, sizeof(err)), err);
      int w = 0, h = 0;
      rp::cfg::screenSize(p, o, w, h);
      TEST_ASSERT_EQUAL(w, gCfg->screenW);
      TEST_ASSERT_EQUAL(h, gCfg->screenH);
      TEST_ASSERT_EQUAL(p, gCfg->panel);
      TEST_ASSERT_EQUAL(o, gCfg->orientation);
      TEST_ASSERT_EQUAL(3, gCfg->pageCount);
    }
  }
}

void test_screen_sizes() {
  int w = 0, h = 0;
  rp::cfg::screenSize(0, 0, w, h);
  TEST_ASSERT_EQUAL(480, w);
  TEST_ASSERT_EQUAL(320, h);
  rp::cfg::screenSize(0, 3, w, h);
  TEST_ASSERT_EQUAL(320, w);
  TEST_ASSERT_EQUAL(480, h);
  rp::cfg::screenSize(1, 1, w, h);
  TEST_ASSERT_EQUAL(240, w);
  TEST_ASSERT_EQUAL(320, h);
  rp::cfg::screenSize(1, 2, w, h);
  TEST_ASSERT_EQUAL(320, w);
  TEST_ASSERT_EQUAL(240, h);
}

void test_source_defaults_fill_style() {
  TEST_ASSERT_TRUE_MESSAGE(parseText(rp::cfg::kDefaultConfigJson), gErr);
  const auto& cpuTemp = gCfg->pages[1].widgets[1];
  TEST_ASSERT_EQUAL_STRING("sys.cpu.temp", cpuTemp.source);
  TEST_ASSERT_EQUAL_STRING("\xC2\xB0" "C", cpuTemp.unit);
  TEST_ASSERT_EQUAL_FLOAT(20, cpuTemp.min);
  TEST_ASSERT_EQUAL_FLOAT(90, cpuTemp.crit);
  const auto& down = gCfg->pages[1].widgets[6];
  TEST_ASSERT_EQUAL_STRING("kB/s", down.unit);
  TEST_ASSERT_TRUE(isnan(down.warn));
  const auto& ram = gCfg->pages[1].widgets[5];
  TEST_ASSERT_EQUAL_FLOAT(85, ram.warn);
}

void test_page_override_merges() {
  const char* json =
      "{\"schema\":1,\"inputs\":{\"enc1\":{\"rotate\":\"nav.page\",\"click\":\"nav.home\"}},"
      "\"pages\":[{\"id\":\"a\",\"inputs_override\":{\"enc1\":{\"click\":\"media.mute\"}}},{\"id\":\"b\"}]}";
  TEST_ASSERT_TRUE_MESSAGE(parseText(json), gErr);
  TEST_ASSERT_TRUE(gCfg->pages[0].inputs.enc[0].click.action == Action::kMute);
  TEST_ASSERT_TRUE(gCfg->pages[0].inputs.enc[0].rotate == Action::kNavPage);
  TEST_ASSERT_TRUE(gCfg->pages[1].inputs.enc[0].click.action == Action::kNavHome);
  TEST_ASSERT_EQUAL_STRING("a", gCfg->pages[0].name);  // name defaults to the id
}

void test_page_grid_and_theme_fall_back_to_global() {
  const char* json =
      "{\"schema\":1,\"grid\":{\"cols\":3,\"rows\":2},\"theme\":{\"name\":\"black\",\"accent\":\"#112233\"},"
      "\"pages\":[{\"id\":\"a\",\"grid\":{\"cols\":8,\"rows\":4},\"theme\":{\"accent\":\"#FF7043\"},"
      "\"widgets\":[{\"type\":\"image\",\"x\":7,\"y\":3,\"w\":1,\"h\":1,\"asset\":\"pic\",\"fit\":\"contain\"}]},{\"id\":\"b\"}]}";
  TEST_ASSERT_TRUE_MESSAGE(parseText(json), gErr);
  const Config& c = *gCfg;
  TEST_ASSERT_EQUAL(8, c.pages[0].cols);
  TEST_ASSERT_EQUAL(4, c.pages[0].rows);
  TEST_ASSERT_EQUAL(2, c.pages[0].theme);  // "black" from the global theme
  TEST_ASSERT_EQUAL_HEX32(0xFF7043, c.pages[0].accent);
  TEST_ASSERT_EQUAL(3, c.pages[1].cols);
  TEST_ASSERT_EQUAL(2, c.pages[1].rows);
  TEST_ASSERT_EQUAL_HEX32(0x112233, c.pages[1].accent);
  TEST_ASSERT_EQUAL_STRING("pic", c.pages[0].widgets[0].asset);
  TEST_ASSERT_TRUE(c.pages[0].widgets[0].fit == rp::cfg::Fit::kContain);
  TEST_ASSERT_EQUAL_STRING("black", rp::cfg::themeName(2));
}

void test_invalid_json() {
  TEST_ASSERT_FALSE(parseText("{\"schema\":1,"));
  TEST_ASSERT_EQUAL(0, strncmp(gErr, "config:", 7));
  TEST_ASSERT_FALSE(parseText("[1,2]"));
  TEST_ASSERT_EQUAL(0, strncmp(gErr, "config:", 7));
}

void test_too_large() {
  static char big[rp::cfg::kMaxConfigBytes + 2];
  memset(big, ' ', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';
  TEST_ASSERT_FALSE(parseText(big));
  TEST_ASSERT_EQUAL(0, strncmp(gErr, "config: too large", 17));
}

void test_shared_vectors() {
  JsonDocument vectors;
  TEST_ASSERT_FALSE(deserializeJson(vectors, kConfigVectorsJson));
  int count = 0;
  for (JsonObjectConst tc : vectors["cases"].as<JsonArrayConst>()) {
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, rp::cfg::kDefaultConfigJson));
    applyPatch(doc, tc["patch"].as<JsonArrayConst>());
    static char json[rp::cfg::kMaxConfigBytes];
    const size_t len = serializeJson(doc, json, sizeof(json));
    // "device" overrides "where" for host-only checks.
    JsonVariantConst where = tc["device"].isUnbound() ? tc["where"] : tc["device"];
    const bool ok = rp::cfg::parse(json, len, *gCfg, gErr, sizeof(gErr));
    char msg[256];
    snprintf(msg, sizeof(msg), "case \"%s\": %s", tc["name"].as<const char*>(), ok ? "accepted" : gErr);
    if (where.isNull()) {
      TEST_ASSERT_TRUE_MESSAGE(ok, msg);
    } else {
      TEST_ASSERT_FALSE_MESSAGE(ok, msg);
      const char* w = where.as<const char*>();
      TEST_ASSERT_EQUAL_MESSAGE(0, strncmp(gErr, w, strlen(w)), msg);
    }
    ++count;
  }
  TEST_ASSERT_TRUE(count > 30);
}

void test_parse_keys() {
  uint8_t m = 0, k = 0;
  TEST_ASSERT_TRUE(rp::actions::parseKeys("ctrl+shift+m", m, k));
  TEST_ASSERT_EQUAL_HEX8(0x03, m);
  TEST_ASSERT_EQUAL_HEX8(0x10, k);  // 'm'
  TEST_ASSERT_TRUE(rp::actions::parseKeys("F13", m, k));
  TEST_ASSERT_EQUAL_HEX8(0x00, m);
  TEST_ASSERT_EQUAL_HEX8(0x68, k);
  TEST_ASSERT_TRUE(rp::actions::parseKeys("win+altgr+e", m, k));
  TEST_ASSERT_EQUAL_HEX8(0x48, m);
  TEST_ASSERT_EQUAL_HEX8(0x08, k);
  TEST_ASSERT_TRUE(rp::actions::parseKeys("alt+f4", m, k));
  TEST_ASSERT_EQUAL_HEX8(0x3D, k);
  TEST_ASSERT_TRUE(rp::actions::parseKeys("0", m, k));
  TEST_ASSERT_EQUAL_HEX8(0x27, k);
  TEST_ASSERT_TRUE(rp::actions::parseKeys("num0", m, k));
  TEST_ASSERT_EQUAL_HEX8(0x62, k);
  TEST_ASSERT_TRUE(rp::actions::parseKeys("f24", m, k));
  TEST_ASSERT_EQUAL_HEX8(0x73, k);
  TEST_ASSERT_FALSE(rp::actions::parseKeys("", m, k));
  TEST_ASSERT_FALSE(rp::actions::parseKeys("f25", m, k));
  TEST_ASSERT_FALSE(rp::actions::parseKeys("ctrl+", m, k));
  TEST_ASSERT_FALSE(rp::actions::parseKeys("a+b", m, k));
}

void test_bindings() {
  const char* json =
      "{\"schema\":1,\"macros\":[{\"id\":\"one\"},{\"id\":\"two\"}],\"inputs\":{\"buttons\":4,"
      "\"btn4\":{\"click\":{\"keys\":\"ctrl+c\"},\"double\":{\"macro\":\"two\"},\"long\":\"media.mute\"}},"
      "\"pages\":[{\"id\":\"a\"}]}";
  TEST_ASSERT_TRUE_MESSAGE(parseText(json), gErr);
  const auto& b = gCfg->inputs.btn[3];
  TEST_ASSERT_TRUE(b.click.kind == rp::actions::Binding::Kind::kKeys);
  TEST_ASSERT_EQUAL_HEX8(0x01, b.click.mods);
  TEST_ASSERT_EQUAL_HEX8(0x06, b.click.key);
  TEST_ASSERT_TRUE(b.dbl.kind == rp::actions::Binding::Kind::kMacro);
  TEST_ASSERT_EQUAL(1, b.dbl.macro);
  TEST_ASSERT_EQUAL_STRING("two", gCfg->macroIds[1]);
  TEST_ASSERT_TRUE(b.lng.action == Action::kMute);
  TEST_ASSERT_TRUE(gCfg->pages[0].inputs.btn[3].dbl.kind == rp::actions::Binding::Kind::kMacro);
  TEST_ASSERT_EQUAL(4, gCfg->buttons);
  // The older "buttons" count means ports 1 to n.
  TEST_ASSERT_EQUAL(1, gCfg->buttonPorts[0]);
  TEST_ASSERT_EQUAL(4, gCfg->buttonPorts[3]);
}

void test_numbering() {
  const char* json =
      "{\"schema\":1,\"inputs\":{\"buttons\":4,\"knob_ports\":[2,0,1],\"button_ports\":[2],"
      "\"enc3\":{\"rotate\":\"media.volume\",\"counts_per_detent\":2,\"invert\":true}},\"pages\":[{\"id\":\"a\"}]}";
  TEST_ASSERT_TRUE_MESSAGE(parseText(json), gErr);
  TEST_ASSERT_EQUAL(2, gCfg->knobPorts[0]);
  TEST_ASSERT_EQUAL(0, gCfg->knobPorts[1]);
  TEST_ASSERT_EQUAL(1, gCfg->knobPorts[2]);
  // button_ports wins over "buttons"; numbers after its end are not used.
  TEST_ASSERT_EQUAL(2, gCfg->buttonPorts[0]);
  TEST_ASSERT_EQUAL(0, gCfg->buttonPorts[1]);
  TEST_ASSERT_EQUAL(0, gCfg->buttonPorts[3]);
  TEST_ASSERT_EQUAL(1, gCfg->buttons);
  TEST_ASSERT_TRUE(gCfg->inputs.enc[2].rotate == Action::kVolume);
  TEST_ASSERT_TRUE(gCfg->pages[0].inputs.enc[2].rotate == Action::kVolume);
  TEST_ASSERT_EQUAL(2, gCfg->countsPerDetent[2]);
  TEST_ASSERT_TRUE(gCfg->invert[2]);
  // Defaults: knob 1 = port 1, knob 2 = port 2, no buttons.
  TEST_ASSERT_TRUE_MESSAGE(parseText("{\"schema\":1,\"pages\":[{\"id\":\"a\"}]}"), gErr);
  TEST_ASSERT_EQUAL(1, gCfg->knobPorts[0]);
  TEST_ASSERT_EQUAL(2, gCfg->knobPorts[1]);
  TEST_ASSERT_EQUAL(0, gCfg->knobPorts[2]);
  TEST_ASSERT_EQUAL(0, gCfg->buttons);
}

void test_actions() {
  Action a;
  TEST_ASSERT_TRUE(rp::actions::parse("media.seek", a));
  TEST_ASSERT_TRUE(a == Action::kSeek);
  TEST_ASSERT_TRUE(rp::actions::isRotate(a));
  TEST_ASSERT_FALSE(rp::actions::parse("media.foo", a));
  TEST_ASSERT_EQUAL_STRING("display.toggle", rp::actions::name(Action::kDisplayToggle));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_default_config);
  RUN_TEST(test_source_defaults_fill_style);
  RUN_TEST(test_page_override_merges);
  RUN_TEST(test_page_grid_and_theme_fall_back_to_global);
  RUN_TEST(test_invalid_json);
  RUN_TEST(test_too_large);
  RUN_TEST(test_shared_vectors);
  RUN_TEST(test_parse_keys);
  RUN_TEST(test_bindings);
  RUN_TEST(test_numbering);
  RUN_TEST(test_actions);
  RUN_TEST(test_default_layouts_fit_every_screen);
  RUN_TEST(test_screen_sizes);
  return UNITY_END();
}
