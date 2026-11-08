// Backlight policy tests. Run: pio test -e native -f test_screen
#include <unity.h>

#include "screen_logic.h"

using namespace rp::screen;

void setUp() {}
void tearDown() {}

namespace {

constexpr int kNoon = 12 * 60;
constexpr int kMidnight = 0;
constexpr int kLate = 23 * 60 + 30;

Settings nightSettings(uint8_t nightLevel = 15) {
  Settings s;
  s.brightness = 80;
  s.nightEnabled = true;
  s.nightFromMin = 23 * 60;
  s.nightToMin = 7 * 60 + 30;
  s.nightBrightness = nightLevel;
  s.sleepOnPcSleep = true;
  return s;
}

}  // namespace

static void test_window_plain_and_wrapping() {
  TEST_ASSERT_TRUE(inWindow(60, 120, 60));
  TEST_ASSERT_FALSE(inWindow(60, 120, 120));
  TEST_ASSERT_FALSE(inWindow(60, 120, 59));
  TEST_ASSERT_TRUE(inWindow(23 * 60, 7 * 60, 23 * 60));
  TEST_ASSERT_TRUE(inWindow(23 * 60, 7 * 60, 0));
  TEST_ASSERT_TRUE(inWindow(23 * 60, 7 * 60, 7 * 60 - 1));
  TEST_ASSERT_FALSE(inWindow(23 * 60, 7 * 60, 7 * 60));
  TEST_ASSERT_FALSE(inWindow(23 * 60, 7 * 60, kNoon));
  TEST_ASSERT_FALSE(inWindow(100, 100, 100));  // empty
  TEST_ASSERT_FALSE(inWindow(23 * 60, 7 * 60, -1));  // time unknown
}

static void test_day_and_night_levels() {
  Policy p;
  p.setSettings(nightSettings());
  TEST_ASSERT_EQUAL(80, p.target(kNoon, 0));
  TEST_ASSERT_EQUAL(15, p.target(kLate, 0));
  TEST_ASSERT_EQUAL(15, p.target(kMidnight, 0));
  TEST_ASSERT_EQUAL(80, p.target(-1, 0));  // no clock: never night
  Settings off = nightSettings();
  off.nightEnabled = false;
  p.setSettings(off);
  TEST_ASSERT_EQUAL(80, p.target(kLate, 0));
}

static void test_knob_changes_current_period_until_next_config() {
  Policy p;
  p.setSettings(nightSettings());
  p.adjust(2, kNoon, 0);
  TEST_ASSERT_EQUAL(90, p.target(kNoon, 0));
  TEST_ASSERT_EQUAL(15, p.target(kLate, 0));
  p.adjust(-10, kLate, 0);
  TEST_ASSERT_EQUAL(kKnobMin, p.target(kLate, 0));
  TEST_ASSERT_EQUAL(90, p.target(kNoon, 0));
  p.adjust(10, kNoon, 0);
  TEST_ASSERT_EQUAL(100, p.target(kNoon, 0));
  p.setSettings(nightSettings());
  TEST_ASSERT_EQUAL(80, p.target(kNoon, 0));
  TEST_ASSERT_EQUAL(15, p.target(kLate, 0));
}

static void test_toggle_off_and_first_input_wakes_only() {
  Policy p;
  p.setSettings(nightSettings());
  TEST_ASSERT_FALSE(p.onInput(kNoon, 0));  // screen on: the input runs its action
  p.turnOff();
  TEST_ASSERT_EQUAL(0, p.target(kNoon, 10));
  TEST_ASSERT_TRUE(p.onInput(kNoon, 20));  // only wakes
  TEST_ASSERT_EQUAL(80, p.target(kNoon, 30));
  TEST_ASSERT_FALSE(p.onInput(kNoon, 40));
}

static void test_pc_sleep_turns_off_and_input_wakes_for_a_while() {
  Policy p;
  p.setSettings(nightSettings());
  p.setUsbSuspended(true);
  TEST_ASSERT_EQUAL(0, p.target(kNoon, 1000));
  TEST_ASSERT_TRUE(p.onInput(kNoon, 1000));
  TEST_ASSERT_EQUAL(80, p.target(kNoon, 1000 + kWakeMs - 1));
  TEST_ASSERT_FALSE(p.onInput(kNoon, 20000));  // already on: runs, and extends the wake
  TEST_ASSERT_EQUAL(80, p.target(kNoon, 20000 + kWakeMs - 1));
  TEST_ASSERT_EQUAL(0, p.target(kNoon, 20000 + kWakeMs));
  p.setUsbSuspended(false);
  TEST_ASSERT_EQUAL(80, p.target(kNoon, 100000));
}

static void test_pc_sleep_ignored_when_disabled() {
  Policy p;
  Settings s = nightSettings();
  s.sleepOnPcSleep = false;
  p.setSettings(s);
  p.setUsbSuspended(true);
  TEST_ASSERT_EQUAL(80, p.target(kNoon, 0));
  TEST_ASSERT_FALSE(p.onInput(kNoon, 0));
}

static void test_suspend_cuts_a_running_wake() {
  Policy p;
  p.setSettings(nightSettings());
  p.onInput(kNoon, 0);
  p.setUsbSuspended(true);
  TEST_ASSERT_EQUAL(0, p.target(kNoon, 10));
}

static void test_night_level_zero_wakes_dim() {
  Policy p;
  p.setSettings(nightSettings(0));
  TEST_ASSERT_EQUAL(0, p.target(kLate, 0));
  TEST_ASSERT_TRUE(p.onInput(kLate, 0));
  TEST_ASSERT_EQUAL(kWakeLevel, p.target(kLate, 100));
  TEST_ASSERT_EQUAL(0, p.target(kLate, kWakeMs));
  TEST_ASSERT_EQUAL(80, p.target(kNoon, kWakeMs));
}

static void test_millis_wrap() {
  Policy p;
  p.setSettings(nightSettings());
  p.setUsbSuspended(true);
  const uint32_t t = 0xFFFFFFFFu - 1000;
  p.onInput(kNoon, t);
  TEST_ASSERT_EQUAL(80, p.target(kNoon, t + 5000));  // wrapped past 0
  TEST_ASSERT_EQUAL(0, p.target(kNoon, t + kWakeMs));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_window_plain_and_wrapping);
  RUN_TEST(test_day_and_night_levels);
  RUN_TEST(test_knob_changes_current_period_until_next_config);
  RUN_TEST(test_toggle_off_and_first_input_wakes_only);
  RUN_TEST(test_pc_sleep_turns_off_and_input_wakes_for_a_while);
  RUN_TEST(test_pc_sleep_ignored_when_disabled);
  RUN_TEST(test_suspend_cuts_a_running_wake);
  RUN_TEST(test_night_level_zero_wakes_dim);
  RUN_TEST(test_millis_wrap);
  return UNITY_END();
}
