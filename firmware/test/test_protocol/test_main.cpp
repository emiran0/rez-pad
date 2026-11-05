// Protocol framing tests against the shared vectors (docs/protocol-test-vectors.json).
// Run: pio test -e native
#include <string.h>
#include <unity.h>

#include "deskhub_protocol.h"
#include "vectors.h"

using namespace dh::proto;

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

void setUp() {}
void tearDown() {}

static void test_crc16() {
  for (size_t i = 0; i < COUNT(kCRC16); ++i) {
    TEST_ASSERT_EQUAL_HEX16(kCRC16[i].crc, crc16(kCRC16[i].data, kCRC16[i].len));
  }
}

static void test_crc32() {
  for (size_t i = 0; i < COUNT(kCRC32); ++i) {
    TEST_ASSERT_EQUAL_HEX32(kCRC32[i].crc, crc32(kCRC32[i].data, kCRC32[i].len));
  }
}

static void test_crc32_incremental() {
  const CrcVector& v = kCRC32[2];
  uint32_t c = crc32(v.data, 100);
  c = crc32(v.data + 100, v.len - 100, c);
  TEST_ASSERT_EQUAL_HEX32(v.crc, c);
}

static void test_cobs_encode() {
  static uint8_t out[600];
  for (size_t i = 0; i < COUNT(kCOBS); ++i) {
    const CobsVector& v = kCOBS[i];
    const size_t n = cobsEncode(v.dec, v.decLen, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(v.encLen, n);
    TEST_ASSERT_EQUAL_MEMORY(v.enc, out, n);
    TEST_ASSERT_TRUE(n <= cobsMaxEncodedSize(v.decLen));
  }
}

static void test_cobs_decode() {
  static uint8_t out[600];
  for (size_t i = 0; i < COUNT(kCOBS); ++i) {
    const CobsVector& v = kCOBS[i];
    const int32_t n = cobsDecode(v.enc, v.encLen, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(v.decLen), n);
    if (v.decLen) TEST_ASSERT_EQUAL_MEMORY(v.dec, out, v.decLen);
  }
}

static void test_cobs_encode_capacity() {
  uint8_t out[4];
  const uint8_t in[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  TEST_ASSERT_EQUAL_size_t(0, cobsEncode(in, sizeof(in), out, sizeof(out)));
}

static void test_build_frame() {
  static uint8_t out[512];
  for (size_t i = 0; i < COUNT(kFRAMES); ++i) {
    const FrameVector& v = kFRAMES[i];
    const size_t n = buildFrame(v.type, v.seq, v.body, v.bodyLen, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t_MESSAGE(v.frameLen, n, v.name);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(v.frame, out, n, v.name);
  }
}

static void test_decoder_frames() {
  static FrameDecoder dec;
  for (size_t i = 0; i < COUNT(kFRAMES); ++i) {
    const FrameVector& v = kFRAMES[i];
    bool got = false;
    for (size_t k = 0; k < v.frameLen; ++k) {
      const bool r = dec.push(v.frame[k]);
      TEST_ASSERT_TRUE_MESSAGE(!r || k == v.frameLen - 1, v.name);
      got = got || r;
    }
    TEST_ASSERT_TRUE_MESSAGE(got, v.name);
    TEST_ASSERT_EQUAL_HEX8(v.type, dec.packet().type);
    TEST_ASSERT_EQUAL_UINT16(v.seq, dec.packet().seq);
    TEST_ASSERT_EQUAL_size_t(v.bodyLen, dec.packet().bodyLen);
    if (v.bodyLen) TEST_ASSERT_EQUAL_MEMORY(v.body, dec.packet().body, v.bodyLen);
  }
  TEST_ASSERT_EQUAL_UINT32(COUNT(kFRAMES), dec.stats().ok);
}

static void test_decoder_bad_frames() {
  static FrameDecoder dec;
  for (size_t i = 0; i < COUNT(kBAD_FRAMES); ++i) {
    const BadFrameVector& v = kBAD_FRAMES[i];
    for (size_t k = 0; k < v.frameLen; ++k) TEST_ASSERT_FALSE_MESSAGE(dec.push(v.frame[k]), v.name);
  }
  TEST_ASSERT_EQUAL_UINT32(0, dec.stats().ok);
  TEST_ASSERT_EQUAL_UINT32(1, dec.stats().badCrc);
  TEST_ASSERT_EQUAL_UINT32(1, dec.stats().tooShort);
  TEST_ASSERT_EQUAL_UINT32(1, dec.stats().badVersion);
  TEST_ASSERT_EQUAL_UINT32(1, dec.stats().badCobs);

  // A good frame right after garbage is still received.
  const FrameVector& v = kFRAMES[0];
  bool got = false;
  for (size_t k = 0; k < v.frameLen; ++k) got = dec.push(v.frame[k]) || got;
  TEST_ASSERT_TRUE(got);
}

static void test_decoder_overflow_recovers() {
  static FrameDecoder dec;
  for (size_t i = 0; i < 10000; ++i) TEST_ASSERT_FALSE(dec.push(0x55));
  TEST_ASSERT_FALSE(dec.push(0x00));
  TEST_ASSERT_EQUAL_UINT32(1, dec.stats().tooLarge);
  const FrameVector& v = kFRAMES[2];
  bool got = false;
  for (size_t k = 0; k < v.frameLen; ++k) got = dec.push(v.frame[k]) || got;
  TEST_ASSERT_TRUE(got);
}

static void test_max_body_roundtrip() {
  static uint8_t body[kMaxBody];
  static uint8_t frame[maxFrameSize(kMaxBody)];
  static FrameDecoder dec;
  for (size_t i = 0; i < kMaxBody; ++i) body[i] = static_cast<uint8_t>(i * 7);
  const size_t n = buildFrame(kArtChunk, 7, body, kMaxBody, frame, sizeof(frame));
  TEST_ASSERT_TRUE(n > 0);
  bool got = false;
  for (size_t k = 0; k < n; ++k) got = dec.push(frame[k]) || got;
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL_size_t(kMaxBody, dec.packet().bodyLen);
  TEST_ASSERT_EQUAL_MEMORY(body, dec.packet().body, kMaxBody);
  TEST_ASSERT_EQUAL_size_t(0, buildFrame(kArtChunk, 7, body, kMaxBody + 1, frame, sizeof(frame)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc16);
  RUN_TEST(test_crc32);
  RUN_TEST(test_crc32_incremental);
  RUN_TEST(test_cobs_encode);
  RUN_TEST(test_cobs_decode);
  RUN_TEST(test_cobs_encode_capacity);
  RUN_TEST(test_build_frame);
  RUN_TEST(test_decoder_frames);
  RUN_TEST(test_decoder_bad_frames);
  RUN_TEST(test_decoder_overflow_recovers);
  RUN_TEST(test_max_body_roundtrip);
  return UNITY_END();
}
