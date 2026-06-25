#include "test_framework.h"
#include "frame.h"

using namespace esphome::balboa_spa;

TEST(frame_parses_ready) {
  auto b = hexb("7e 05 10 bf 06 5c 7e");
  ParsedFrame f{};
  size_t consumed = 0;
  CHECK(scan_frame(b.data(), b.size(), &f, &consumed) == ScanResult::FRAME);
  CHECK_EQ(consumed, 7);
  CHECK_EQ(f.src, 0x10);
  CHECK_EQ(f.type0, 0xbf);
  CHECK_EQ(f.type1, 0x06);
  CHECK_EQ(f.payload_len, 0);
}

TEST(frame_skips_leading_garbage) {
  auto b = hexb("11 22 33 7e 05 10 bf 06 5c 7e");
  ParsedFrame f{};
  size_t consumed = 0;
  CHECK(scan_frame(b.data(), b.size(), &f, &consumed) == ScanResult::FRAME);
  CHECK_EQ(consumed, 10);
  CHECK_EQ(f.type1, 0x06);
}

TEST(frame_needs_more_on_partial) {
  auto b = hexb("7e 05 10 bf");  // truncated
  ParsedFrame f{};
  size_t consumed = 0;
  CHECK(scan_frame(b.data(), b.size(), &f, &consumed) == ScanResult::NEED_MORE);
  CHECK_EQ(consumed, 0);  // keep the partial starting at 0x7e
}

TEST(frame_rejects_bad_crc_and_resyncs) {
  // first frame has wrong CRC (5d), a valid Ready follows
  auto b = hexb("7e 05 10 bf 06 5d 7e 7e 05 10 bf 06 5c 7e");
  ParsedFrame f{};
  size_t consumed = 0;
  CHECK(scan_frame(b.data(), b.size(), &f, &consumed) == ScanResult::FRAME);
  CHECK_EQ(consumed, 14);  // discards the bad frame, returns the good one
}

TEST(frame_parses_status_payload) {
  auto b = hexb("7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 01 00 02 03 "
                "00 00 00 00 00 66 00 00 00 0f 7e");
  ParsedFrame f{};
  size_t consumed = 0;
  CHECK(scan_frame(b.data(), b.size(), &f, &consumed) == ScanResult::FRAME);
  CHECK_EQ(f.src, 0xff);
  CHECK_EQ(f.type0, 0xaf);
  CHECK_EQ(f.type1, 0x13);
  CHECK_EQ(f.payload_len, 24);
  CHECK_EQ(f.payload[2], 0x64);   // current temp byte
  CHECK_EQ(f.payload[20], 0x66);  // target temp byte
}

TEST(frame_build_config_request) {
  uint8_t out[16];
  size_t n = build_frame(out, 0x0a, 0xbf, 0x04, nullptr, 0);
  auto expect = hexb("7e 05 0a bf 04 77 7e");
  CHECK_EQ(n, expect.size());
  for (size_t i = 0; i < n; i++) CHECK_EQ(out[i], expect[i]);
}

TEST(frame_build_toggle_light1) {
  uint8_t out[16];
  uint8_t payload[2] = {0x11, 0x00};
  size_t n = build_frame(out, 0x0a, 0xbf, 0x11, payload, 2);
  auto expect = hexb("7e 07 0a bf 11 11 00 93 7e");
  CHECK_EQ(n, expect.size());
  for (size_t i = 0; i < n; i++) CHECK_EQ(out[i], expect[i]);
}

TEST(frame_build_then_scan_roundtrip) {
  uint8_t out[16];
  uint8_t payload[1] = {0x64};
  size_t n = build_frame(out, 0x0a, 0xbf, 0x20, payload, 1);
  ParsedFrame f{};
  size_t consumed = 0;
  CHECK(scan_frame(out, n, &f, &consumed) == ScanResult::FRAME);
  CHECK_EQ(consumed, n);
  CHECK_EQ(f.type1, 0x20);
  CHECK_EQ(f.payload_len, 1);
  CHECK_EQ(f.payload[0], 0x64);
}
