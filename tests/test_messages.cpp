#include "test_framework.h"
#include "frame.h"
#include "messages.h"

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
