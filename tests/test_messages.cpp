#include "test_framework.h"
#include "frame.h"
#include "messages.h"
#include <string>

using namespace esphome::balboa_spa;

// Static storage to keep hexb results alive for frame parsing
static std::vector<uint8_t> g_frame_buffer;

static ParsedFrame parse(const std::vector<uint8_t> &b) {
  g_frame_buffer = b;  // Keep the data alive
  ParsedFrame f{};
  size_t consumed = 0;
  scan_frame(g_frame_buffer.data(), g_frame_buffer.size(), &f, &consumed);
  return f;
}

TEST(decode_status_fields) {
  auto f = parse(hexb("7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 01 00 02 "
                      "03 00 00 00 00 00 66 00 00 00 0f 7e"));
  SpaStatus s{};
  CHECK(decode_status(f, &s));
  CHECK(s.valid);
  CHECK_EQ(s.current_temp_raw, 100);
  CHECK(s.current_temp_valid);
  CHECK_EQ(s.target_temp_raw, 102);
  CHECK_EQ(s.hour, 14);
  CHECK_EQ(s.minute, 30);
  CHECK(s.heating);
  CHECK(s.temp_range == TempRange::HIGH);
  CHECK(s.temp_scale == TempScale::FAHRENHEIT);
  CHECK_EQ(s.pumps[0], 1);
  CHECK_EQ(s.pumps[1], 0);
  CHECK(s.circulation_pump);
  CHECK(s.lights[0]);
  CHECK(!s.hold);
  CHECK(!s.priming);
}

TEST(decode_control_config_model_version) {
  auto f = parse(hexb("7e 1a 0a bf 24 64 dc 11 00 42 46 42 50 32 30 20 20 01 "
                      "3d 12 38 2e 01 0a 04 00 9b 7e"));
  SpaInfo info{};
  CHECK(decode_control_config(f, &info));
  CHECK(info.valid);
  CHECK(std::string(info.model) == "BFBP20");
  CHECK(std::string(info.version) == "V17.0");
}

TEST(decode_control_config2_inventory) {
  auto f = parse(hexb("7e 0b 0a bf 2e 0a 00 01 d0 00 44 6f 7e"));
  SpaConfig c{};
  CHECK(decode_control_config2(f, &c));
  CHECK(c.valid);
  CHECK_EQ(c.pumps[0], 2);
  CHECK_EQ(c.pumps[1], 2);
  CHECK_EQ(c.pumps[2], 0);
  CHECK(c.lights[0]);
  CHECK(!c.lights[1]);
  CHECK(c.circulation_pump);
  CHECK_EQ(c.blower, 0);
  CHECK(!c.mister);
}

TEST(decode_filter_cycles_fields) {
  auto f = parse(hexb("7e 0d 0a bf 23 08 00 02 00 94 00 01 1e d0 7e"));
  FilterCyclesData fc{};
  CHECK(decode_filter_cycles(f, &fc));
  CHECK(fc.valid);
  CHECK_EQ(fc.c1_start_hour, 8);
  CHECK_EQ(fc.c1_start_minute, 0);
  CHECK_EQ(fc.c1_duration_min, 120);
  CHECK(fc.c2_enabled);
  CHECK_EQ(fc.c2_start_hour, 20);
  CHECK_EQ(fc.c2_start_minute, 0);
  CHECK_EQ(fc.c2_duration_min, 90);
}

TEST(encode_filter_cycles_roundtrip) {
  FilterCyclesData fc{};
  fc.c1_start_hour = 8; fc.c1_start_minute = 0; fc.c1_duration_min = 120;
  fc.c2_enabled = true; fc.c2_start_hour = 20; fc.c2_start_minute = 0; fc.c2_duration_min = 90;
  uint8_t out[32];
  size_t n = encode_filter_cycles(out, fc);
  auto expect = hexb("7e 0d 0a bf 23 08 00 02 00 94 00 01 1e d0 7e");
  CHECK_EQ(n, expect.size());
  for (size_t i = 0; i < n; i++) CHECK_EQ(out[i], expect[i]);
}
