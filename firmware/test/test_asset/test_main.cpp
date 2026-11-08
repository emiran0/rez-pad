// Asset name and RPA1 container tests. Run: pio test -e native
// The same name and bytes are checked by the host tests (AssetTests.cs).
#include <string.h>
#include <unity.h>

#include "asset_format.h"

using rp::proto::RpaFrame;
using rp::proto::RpaReader;

void setUp() {}
void tearDown() {}

void test_asset_name() {
  char name[rp::proto::kAssetNameMax];
  TEST_ASSERT_TRUE(rp::proto::assetName(name, sizeof(name), "3f9a0c1d2e4b", 118, 98, false, 0x1B1F25));
  TEST_ASSERT_EQUAL_STRING("3f9a0c1d2e4b_118x98_c_1b1f25.rpa", name);
  TEST_ASSERT_TRUE(rp::proto::assetName(name, sizeof(name), "ab", 16, 16, true, 0x000000));
  TEST_ASSERT_EQUAL_STRING("ab_16x16_f_000000.rpa", name);
  TEST_ASSERT_FALSE(rp::proto::assetName(name, sizeof(name), "../x", 16, 16, true, 0));
  TEST_ASSERT_FALSE(rp::proto::assetName(name, sizeof(name), "ABC", 16, 16, true, 0));
  TEST_ASSERT_FALSE(rp::proto::assetName(name, sizeof(name), "", 16, 16, true, 0));
  TEST_ASSERT_FALSE(rp::proto::assetName(name, 10, "3f9a0c1d2e4b", 118, 98, false, 0));
}

// "RPA1", 2x3, 2 frames: (100 ms, "AB"), (40 ms, "CDE")
static const uint8_t kFile[] = {'R', 'P', 'A', '1', 2, 0, 3, 0, 2, 0, 0, 0,
                                100, 0, 2, 0, 0, 0, 'A', 'B',
                                40, 0, 3, 0, 0, 0, 'C', 'D', 'E'};

void test_rpa_reads_frames() {
  RpaReader r;
  TEST_ASSERT_TRUE(r.open(kFile, sizeof(kFile)));
  TEST_ASSERT_EQUAL(2, r.width());
  TEST_ASSERT_EQUAL(3, r.height());
  TEST_ASSERT_EQUAL(2, r.frames());
  RpaFrame f;
  TEST_ASSERT_TRUE(r.next(f));
  TEST_ASSERT_EQUAL(100, f.delayMs);
  TEST_ASSERT_EQUAL(2, f.len);
  TEST_ASSERT_EQUAL_MEMORY("AB", f.jpeg, 2);
  TEST_ASSERT_TRUE(r.next(f));
  TEST_ASSERT_EQUAL(40, f.delayMs);
  TEST_ASSERT_EQUAL_MEMORY("CDE", f.jpeg, 3);
  TEST_ASSERT_FALSE(r.next(f));
}

void test_rpa_rejects_bad_input() {
  RpaReader r;
  TEST_ASSERT_FALSE(r.open(kFile, 8));
  uint8_t bad[sizeof(kFile)];
  memcpy(bad, kFile, sizeof(kFile));
  bad[3] = '2';
  TEST_ASSERT_FALSE(r.open(bad, sizeof(bad)));
  // Truncated second frame: the first still reads, the second does not.
  TEST_ASSERT_TRUE(r.open(kFile, sizeof(kFile) - 1));
  RpaFrame f;
  TEST_ASSERT_TRUE(r.next(f));
  TEST_ASSERT_FALSE(r.next(f));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_asset_name);
  RUN_TEST(test_rpa_reads_frames);
  RUN_TEST(test_rpa_rejects_bad_input);
  return UNITY_END();
}
