// Album art reassembly tests. Run: pio test -e native
#include <string.h>
#include <unity.h>

#include "art_assembler.h"
#include "rezpad_protocol.h"

using rp::proto::ArtAssembler;
using R = ArtAssembler::Result;

void setUp() {}
void tearDown() {}

static uint8_t gImage[10000];
static uint8_t gBuf[16384];
static uint8_t gChunk[8 + 4096];

static size_t makeChunk(uint32_t id, uint32_t offset, const uint8_t* data, size_t n) {
  for (int i = 0; i < 4; ++i) {
    gChunk[i] = static_cast<uint8_t>(id >> (8 * i));
    gChunk[4 + i] = static_cast<uint8_t>(offset >> (8 * i));
  }
  memcpy(gChunk + 8, data, n);
  return 8 + n;
}

static void fillImage() {
  for (size_t i = 0; i < sizeof(gImage); ++i) gImage[i] = static_cast<uint8_t>(i * 7 + 3);
}

// Sends the whole image in 4096-byte chunks; returns the last chunk result.
static R sendAll(ArtAssembler& a, uint32_t id, size_t size) {
  R r = R::kOk;
  for (size_t off = 0; off < size && r == R::kOk; off += 4096) {
    const size_t n = size - off < 4096 ? size - off : 4096;
    r = a.chunk(gChunk, makeChunk(id, static_cast<uint32_t>(off), gImage + off, n));
  }
  return r;
}

static void test_complete_transfer() {
  fillImage();
  ArtAssembler a(gBuf, sizeof(gBuf));
  const uint32_t crc = rp::proto::crc32(gImage, sizeof(gImage));
  TEST_ASSERT_EQUAL(R::kOk, a.begin(42, sizeof(gImage), crc));
  TEST_ASSERT_TRUE(a.active());
  TEST_ASSERT_EQUAL(R::kOk, sendAll(a, 42, sizeof(gImage)));
  TEST_ASSERT_EQUAL(R::kOk, a.end(42));
  TEST_ASSERT_FALSE(a.active());
  TEST_ASSERT_EQUAL(sizeof(gImage), a.size());
  TEST_ASSERT_EQUAL_MEMORY(gImage, a.data(), sizeof(gImage));
}

static void test_bad_crc() {
  fillImage();
  ArtAssembler a(gBuf, sizeof(gBuf));
  a.begin(1, sizeof(gImage), 0x12345678);
  sendAll(a, 1, sizeof(gImage));
  TEST_ASSERT_EQUAL(R::kBadCrc, a.end(1));
}

static void test_too_large() {
  ArtAssembler a(gBuf, sizeof(gBuf));
  TEST_ASSERT_EQUAL(R::kTooLarge, a.begin(1, sizeof(gBuf) + 1, 0));
  TEST_ASSERT_FALSE(a.active());
  TEST_ASSERT_EQUAL(R::kTooLarge, a.begin(1, 0, 0));
}

static void test_out_of_order_aborts() {
  fillImage();
  ArtAssembler a(gBuf, sizeof(gBuf));
  a.begin(5, sizeof(gImage), rp::proto::crc32(gImage, sizeof(gImage)));
  TEST_ASSERT_EQUAL(R::kBadChunk, a.chunk(gChunk, makeChunk(5, 4096, gImage + 4096, 4096)));
  TEST_ASSERT_FALSE(a.active());
  TEST_ASSERT_EQUAL(R::kIgnored, a.end(5));
}

static void test_overrun_aborts() {
  fillImage();
  ArtAssembler a(gBuf, sizeof(gBuf));
  a.begin(5, 100, 0);
  TEST_ASSERT_EQUAL(R::kBadChunk, a.chunk(gChunk, makeChunk(5, 0, gImage, 101)));
}

static void test_short_chunk() {
  ArtAssembler a(gBuf, sizeof(gBuf));
  const uint8_t tiny[3] = {1, 2, 3};
  TEST_ASSERT_EQUAL(R::kIgnored, a.chunk(tiny, sizeof(tiny)));  // nothing active
  a.begin(5, 100, 0);
  TEST_ASSERT_EQUAL(R::kBadChunk, a.chunk(tiny, sizeof(tiny)));
}

static void test_end_before_all_bytes() {
  fillImage();
  ArtAssembler a(gBuf, sizeof(gBuf));
  a.begin(9, sizeof(gImage), rp::proto::crc32(gImage, sizeof(gImage)));
  a.chunk(gChunk, makeChunk(9, 0, gImage, 4096));
  TEST_ASSERT_EQUAL(R::kIncomplete, a.end(9));
}

static void test_new_begin_supersedes() {
  fillImage();
  ArtAssembler a(gBuf, sizeof(gBuf));
  a.begin(1, sizeof(gImage), 0);
  a.chunk(gChunk, makeChunk(1, 0, gImage, 4096));
  const uint32_t crc = rp::proto::crc32(gImage, 5000);
  TEST_ASSERT_EQUAL(R::kOk, a.begin(2, 5000, crc));
  // Late chunk and end of the old transfer are ignored.
  TEST_ASSERT_EQUAL(R::kIgnored, a.chunk(gChunk, makeChunk(1, 4096, gImage + 4096, 4096)));
  TEST_ASSERT_EQUAL(R::kIgnored, a.end(1));
  TEST_ASSERT_TRUE(a.active());
  TEST_ASSERT_EQUAL(R::kOk, sendAll(a, 2, 5000));
  TEST_ASSERT_EQUAL(R::kOk, a.end(2));
  TEST_ASSERT_EQUAL_MEMORY(gImage, a.data(), 5000);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_complete_transfer);
  RUN_TEST(test_bad_crc);
  RUN_TEST(test_too_large);
  RUN_TEST(test_out_of_order_aborts);
  RUN_TEST(test_overrun_aborts);
  RUN_TEST(test_short_chunk);
  RUN_TEST(test_end_before_all_bytes);
  RUN_TEST(test_new_begin_supersedes);
  return UNITY_END();
}
