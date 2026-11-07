// Device-side connection state tests. Run: pio test -e native
#include <unity.h>

#include "link_monitor.h"

using rp::proto::LinkMonitor;

void setUp() {}
void tearDown() {}

static void test_connects_after_hello() {
  LinkMonitor m;
  TEST_ASSERT_FALSE(m.update(0));
  TEST_ASSERT_TRUE(m.onHello(100, 1));
  TEST_ASSERT_TRUE(m.update(100));
  TEST_ASSERT_TRUE(m.connected());
}

static void test_rejects_other_protocol() {
  LinkMonitor m;
  TEST_ASSERT_FALSE(m.onHello(100, 2));
  m.update(100);
  TEST_ASSERT_FALSE(m.connected());
}

static void test_stays_connected_with_pings() {
  LinkMonitor m;
  m.onHello(0, 1);
  m.update(0);
  for (uint32_t t = 1000; t <= 20000; t += 1000) {
    m.onPing(t);
    TEST_ASSERT_FALSE(m.update(t + 500));
    TEST_ASSERT_TRUE(m.connected());
  }
}

static void test_lost_after_three_seconds() {
  LinkMonitor m;
  m.onHello(0, 1);
  m.update(0);
  m.onPing(1000);
  TEST_ASSERT_FALSE(m.update(4000));  // exactly 3 s: still connected
  TEST_ASSERT_TRUE(m.update(4001));
  TEST_ASSERT_FALSE(m.connected());
  // Pings alone do not reconnect: a new HELLO is required.
  m.onPing(5000);
  m.update(5000);
  TEST_ASSERT_FALSE(m.connected());
  m.onHello(5100, 1);
  TEST_ASSERT_TRUE(m.update(5100));
  TEST_ASSERT_TRUE(m.connected());
}

static void test_need_hello_once_for_orphan_pings() {
  LinkMonitor m;
  m.onPing(0);
  TEST_ASSERT_FALSE(m.takeNeedHello(500));
  m.onPing(1000);
  TEST_ASSERT_TRUE(m.takeNeedHello(1000));
  m.onPing(2000);
  TEST_ASSERT_FALSE(m.takeNeedHello(2000));  // only once
  m.onHello(2100, 1);
  TEST_ASSERT_FALSE(m.takeNeedHello(3000));
}

static void test_reset_disconnects() {
  LinkMonitor m;
  m.onHello(0, 1);
  m.update(0);
  m.reset();
  TEST_ASSERT_FALSE(m.connected());
  TEST_ASSERT_FALSE(m.update(10));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_connects_after_hello);
  RUN_TEST(test_rejects_other_protocol);
  RUN_TEST(test_stays_connected_with_pings);
  RUN_TEST(test_lost_after_three_seconds);
  RUN_TEST(test_need_hello_once_for_orphan_pings);
  RUN_TEST(test_reset_disconnects);
  return UNITY_END();
}
