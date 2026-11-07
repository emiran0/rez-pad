// Gesture and detent logic tests. Run: pio test -e native
#include <unity.h>

#include <vector>

#include "input_logic.h"

using namespace rp::input;

void setUp() {}
void tearDown() {}

namespace {

struct Step {
  uint32_t untilMs;
  bool pressed;
};

// Simulates 1 ms ticks of a switch following `script`; returns the gestures with their times.
std::vector<std::pair<uint32_t, Gesture>> run(GestureDetector& g, const std::vector<Step>& script,
                                              uint32_t endMs) {
  std::vector<std::pair<uint32_t, Gesture>> out;
  size_t idx = 0;
  for (uint32_t t = 0; t <= endMs; ++t) {
    while (idx < script.size() && t >= script[idx].untilMs) ++idx;
    const bool pressed = idx < script.size() ? script[idx].pressed : false;
    const Gesture e = g.update(pressed, t);
    if (e != Gesture::kNone) out.push_back({t, e});
  }
  return out;
}

}  // namespace

static void test_single_click_waits_for_double_window() {
  GestureDetector g;
  // released until 100, pressed 100..200, then released
  auto ev = run(g, {{100, false}, {200, true}}, 1000);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(Gesture::kClick, ev[0].second);
  // release debounced at 220, click confirmed after 300 ms more
  TEST_ASSERT_TRUE(ev[0].first > 520 && ev[0].first < 530);
}

static void test_click_immediate_without_double() {
  GestureConfig cfg;
  cfg.doubleEnabled = false;
  GestureDetector g(cfg);
  auto ev = run(g, {{100, false}, {200, true}}, 1000);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(Gesture::kClick, ev[0].second);
  TEST_ASSERT_TRUE(ev[0].first >= 220 && ev[0].first < 230);
}

static void test_double_click() {
  GestureDetector g;
  auto ev = run(g, {{100, false}, {180, true}, {300, false}, {380, true}}, 1500);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(Gesture::kDouble, ev[0].second);
}

static void test_two_slow_clicks_are_two_clicks() {
  GestureDetector g;
  auto ev = run(g, {{100, false}, {180, true}, {700, false}, {780, true}}, 2000);
  TEST_ASSERT_EQUAL(2, ev.size());
  TEST_ASSERT_EQUAL(Gesture::kClick, ev[0].second);
  TEST_ASSERT_EQUAL(Gesture::kClick, ev[1].second);
}

static void test_long_press_fires_while_held_once() {
  GestureDetector g;
  auto ev = run(g, {{100, false}, {1500, true}}, 2500);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(Gesture::kLong, ev[0].second);
  // press debounced at 120, long at 120 + 600
  TEST_ASSERT_TRUE(ev[0].first >= 720 && ev[0].first < 730);
}

static void test_bounce_is_ignored() {
  GestureDetector g;
  // 5 ms chatter around the press and release must still give exactly one click
  auto ev = run(g, {{100, false}, {103, true}, {106, false}, {109, true}, {200, true},
                    {203, false}, {206, true}, {209, false}},
                2000);
  // after 209 the script ends, pressed=false
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(Gesture::kClick, ev[0].second);
}

static void test_glitch_shorter_than_debounce() {
  GestureDetector g;
  auto ev = run(g, {{100, false}, {110, true}}, 1000);  // 10 ms pulse
  TEST_ASSERT_EQUAL(0, ev.size());
}

static void test_click_then_hold_is_long() {
  GestureDetector g;
  auto ev = run(g, {{100, false}, {180, true}, {300, false}, {1200, true}}, 2000);
  TEST_ASSERT_EQUAL(1, ev.size());
  TEST_ASSERT_EQUAL(Gesture::kLong, ev[0].second);
}

static void test_cancel_press_suppresses_gestures() {
  GestureDetector g;
  std::vector<Gesture> out;
  // Press at 100 (debounced 120), cancelled at 300 by a turn, held until 1500, released.
  for (uint32_t t = 0; t <= 2500; ++t) {
    const bool pressed = t >= 100 && t < 1500;
    if (t == 300) g.cancelPress();
    const Gesture e = g.update(pressed, t);
    if (e != Gesture::kNone) out.push_back(e);
  }
  TEST_ASSERT_EQUAL(0, out.size());

  // The next normal click still works.
  for (uint32_t t = 2500; t <= 3500; ++t) {
    const Gesture e = g.update(t >= 2600 && t < 2700, t);
    if (e != Gesture::kNone) out.push_back(e);
  }
  TEST_ASSERT_EQUAL(1, out.size());
  TEST_ASSERT_EQUAL(Gesture::kClick, out[0]);
}

static void test_cancel_second_press_of_double() {
  GestureDetector g;
  std::vector<Gesture> out;
  // click, then press again and turn: neither click nor double must fire.
  for (uint32_t t = 0; t <= 2000; ++t) {
    const bool pressed = (t >= 100 && t < 180) || (t >= 300 && t < 900);
    if (t == 400) g.cancelPress();
    const Gesture e = g.update(pressed, t);
    if (e != Gesture::kNone) out.push_back(e);
  }
  TEST_ASSERT_EQUAL(0, out.size());
}

static void test_detent_two_counts() {
  DetentAccumulator d;
  d.configure(2, false);
  d.reset(0);
  TEST_ASSERT_EQUAL(0, d.update(1));
  TEST_ASSERT_EQUAL(1, d.update(2));
  TEST_ASSERT_EQUAL(2, d.update(6));
  TEST_ASSERT_EQUAL(0, d.update(7));
  TEST_ASSERT_EQUAL(-1, d.update(4));  // 7 -> 4 crosses one boundary back (base 6 -> 4)
  TEST_ASSERT_EQUAL(-2, d.update(0));
}

static void test_detent_four_counts_and_invert() {
  DetentAccumulator d;
  d.configure(4, true);
  d.reset(100);
  TEST_ASSERT_EQUAL(0, d.update(103));
  TEST_ASSERT_EQUAL(-1, d.update(104));
  TEST_ASSERT_EQUAL(3, d.update(92));
}

static void test_detent_fast_spin_no_loss() {
  DetentAccumulator d;
  d.configure(2, false);
  d.reset(0);
  int total = 0;
  for (int32_t c = 0; c <= 2000; c += 7) total += d.update(c);
  total += d.update(2000);
  TEST_ASSERT_EQUAL(1000, total);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_click_waits_for_double_window);
  RUN_TEST(test_click_immediate_without_double);
  RUN_TEST(test_double_click);
  RUN_TEST(test_two_slow_clicks_are_two_clicks);
  RUN_TEST(test_long_press_fires_while_held_once);
  RUN_TEST(test_bounce_is_ignored);
  RUN_TEST(test_glitch_shorter_than_debounce);
  RUN_TEST(test_click_then_hold_is_long);
  RUN_TEST(test_cancel_press_suppresses_gestures);
  RUN_TEST(test_cancel_second_press_of_double);
  RUN_TEST(test_detent_two_counts);
  RUN_TEST(test_detent_four_counts_and_invert);
  RUN_TEST(test_detent_fast_spin_no_loss);
  return UNITY_END();
}
