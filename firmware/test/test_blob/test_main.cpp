// Config transfer reassembly tests. Run: pio test -e native
#include <string.h>
#include <unity.h>

#include "blob_assembler.h"
#include "rezpad_protocol.h"

using rp::proto::BlobAssembler;
using R = BlobAssembler::Result;

void setUp() {}
void tearDown() {}

static uint8_t gData[5000];
static uint8_t gBuf[8192];
static uint8_t gChunk[4 + 4096];

static size_t makeChunk(uint32_t offset, const uint8_t* data, size_t n) {
  for (int i = 0; i < 4; ++i) gChunk[i] = static_cast<uint8_t>(offset >> (8 * i));
  memcpy(gChunk + 4, data, n);
  return 4 + n;
}

static void fill() {
  for (size_t i = 0; i < sizeof(gData); ++i) gData[i] = static_cast<uint8_t>(i * 13 + 1);
}

void test_complete_transfer() {
  fill();
  BlobAssembler a(gBuf, sizeof(gBuf));
  const uint32_t crc = rp::proto::crc32(gData, sizeof(gData));
  TEST_ASSERT_TRUE(a.begin(sizeof(gData), crc) == R::kOk);
  TEST_ASSERT_TRUE(a.chunk(gChunk, makeChunk(0, gData, 4096)) == R::kOk);
  TEST_ASSERT_TRUE(a.chunk(gChunk, makeChunk(4096, gData + 4096, sizeof(gData) - 4096)) == R::kOk);
  TEST_ASSERT_TRUE(a.end() == R::kOk);
  TEST_ASSERT_EQUAL(sizeof(gData), a.size());
  TEST_ASSERT_EQUAL_HEX32(crc, a.crc());
  TEST_ASSERT_EQUAL_MEMORY(gData, a.data(), sizeof(gData));
}

void test_bad_crc() {
  fill();
  BlobAssembler a(gBuf, sizeof(gBuf));
  TEST_ASSERT_TRUE(a.begin(100, 0x12345678) == R::kOk);
  TEST_ASSERT_TRUE(a.chunk(gChunk, makeChunk(0, gData, 100)) == R::kOk);
  TEST_ASSERT_TRUE(a.end() == R::kBadCrc);
  TEST_ASSERT_FALSE(a.active());
}

void test_too_large_or_empty() {
  BlobAssembler a(gBuf, sizeof(gBuf));
  TEST_ASSERT_TRUE(a.begin(sizeof(gBuf) + 1, 0) == R::kTooLarge);
  TEST_ASSERT_TRUE(a.begin(0, 0) == R::kTooLarge);
  TEST_ASSERT_FALSE(a.active());
}

void test_out_of_order_aborts() {
  fill();
  BlobAssembler a(gBuf, sizeof(gBuf));
  a.begin(200, rp::proto::crc32(gData, 200));
  TEST_ASSERT_TRUE(a.chunk(gChunk, makeChunk(100, gData + 100, 100)) == R::kBadChunk);
  TEST_ASSERT_TRUE(a.chunk(gChunk, makeChunk(0, gData, 100)) == R::kIgnored);
  TEST_ASSERT_TRUE(a.end() == R::kIgnored);
}

void test_overrun_and_short_chunk() {
  fill();
  BlobAssembler a(gBuf, sizeof(gBuf));
  a.begin(50, 0);
  TEST_ASSERT_TRUE(a.chunk(gChunk, makeChunk(0, gData, 51)) == R::kBadChunk);
  a.begin(50, 0);
  TEST_ASSERT_TRUE(a.chunk(gChunk, 4) == R::kBadChunk);
}

void test_end_before_all_bytes() {
  fill();
  BlobAssembler a(gBuf, sizeof(gBuf));
  a.begin(200, rp::proto::crc32(gData, 200));
  a.chunk(gChunk, makeChunk(0, gData, 100));
  TEST_ASSERT_TRUE(a.end() == R::kIncomplete);
}

void test_new_begin_restarts() {
  fill();
  BlobAssembler a(gBuf, sizeof(gBuf));
  a.begin(200, 0);
  a.chunk(gChunk, makeChunk(0, gData, 100));
  TEST_ASSERT_TRUE(a.begin(10, rp::proto::crc32(gData, 10)) == R::kOk);
  TEST_ASSERT_TRUE(a.chunk(gChunk, makeChunk(0, gData, 10)) == R::kOk);
  TEST_ASSERT_TRUE(a.end() == R::kOk);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_complete_transfer);
  RUN_TEST(test_bad_crc);
  RUN_TEST(test_too_large_or_empty);
  RUN_TEST(test_out_of_order_aborts);
  RUN_TEST(test_overrun_and_short_chunk);
  RUN_TEST(test_end_before_all_bytes);
  RUN_TEST(test_new_begin_restarts);
  return UNITY_END();
}
