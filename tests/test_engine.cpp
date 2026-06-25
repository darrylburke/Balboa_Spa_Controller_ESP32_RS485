#include "test_framework.h"
#include "protocol_engine.h"
#include "messages.h"
#include <vector>

using namespace esphome::balboa_spa;

struct Sink {
  std::vector<std::vector<uint8_t>> writes;
  void operator()(const uint8_t *d, size_t n) { writes.emplace_back(d, d + n); }
};

static void feed_hex(ProtocolEngine &e, const char *hex) {
  auto b = hexb(hex);
  e.feed(b.data(), b.size());
}

TEST(engine_decodes_status_and_fires_callback) {
  ProtocolEngine e;
  int fired = 0;
  e.on_status_update = [&] { fired++; };
  feed_hex(e, "7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 01 00 02 03 "
              "00 00 00 00 00 66 00 00 00 0f 7e");
  CHECK(e.status().valid);
  CHECK_EQ(e.status().current_temp_raw, 100);
  CHECK_EQ(fired, 1);
}

TEST(engine_sends_queued_command_only_on_ready) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  // Must have seen a Status before TX is allowed:
  feed_hex(e, "7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 01 00 02 03 "
              "00 00 00 00 00 66 00 00 00 0f 7e");
  e.toggle_item(item::LIGHT1);
  CHECK_EQ(sink.writes.size(), 0);  // nothing sent yet (no Ready)
  // With incomplete config, auto-request enqueues 3 frames, then toggle adds 1 = 4 total
  // Feed 4 Readys to drain the queue:
  feed_hex(e, "7e 05 10 bf 06 5c 7e");  // Ready -> sends first queued (info request)
  CHECK_EQ(sink.writes.size(), 1);
  feed_hex(e, "7e 05 10 bf 06 5c 7e");  // Ready -> sends second queued (config2 request)
  CHECK_EQ(sink.writes.size(), 2);
  feed_hex(e, "7e 05 10 bf 06 5c 7e");  // Ready -> sends third queued (filter request)
  CHECK_EQ(sink.writes.size(), 3);
  feed_hex(e, "7e 05 10 bf 06 5c 7e");  // Ready -> sends toggle
  CHECK_EQ(sink.writes.size(), 4);
  auto expect = hexb("7e 07 0a bf 11 11 00 93 7e");
  CHECK(sink.writes[3] == expect);  // the toggle is now at index 3
  feed_hex(e, "7e 05 10 bf 06 5c 7e");  // next Ready, queue empty -> nothing
  CHECK_EQ(sink.writes.size(), 4);
}

TEST(engine_read_only_suppresses_tx) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  e.set_read_only(true);
  feed_hex(e, "7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 01 00 02 03 "
              "00 00 00 00 00 66 00 00 00 0f 7e");
  e.toggle_item(item::LIGHT1);
  feed_hex(e, "7e 05 10 bf 06 5c 7e");
  CHECK_EQ(sink.writes.size(), 0);
}

TEST(engine_no_tx_before_first_status) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  e.toggle_item(item::LIGHT1);
  feed_hex(e, "7e 05 10 bf 06 5c 7e");  // Ready but no Status yet
  CHECK_EQ(sink.writes.size(), 0);
}

TEST(engine_requests_config_after_status_when_incomplete) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  feed_hex(e, "7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 01 00 02 03 "
              "00 00 00 00 00 66 00 00 00 0f 7e");  // status only -> config incomplete
  // 3 requests should be queued (info, config2, filter). Drain on Readys:
  feed_hex(e, "7e 05 10 bf 06 5c 7e");
  feed_hex(e, "7e 05 10 bf 06 5c 7e");
  feed_hex(e, "7e 05 10 bf 06 5c 7e");
  CHECK_EQ(sink.writes.size(), 3);
  // first queued is the info request (control config request type 1)
  // verify it is a 0a bf 22 request frame
  CHECK_EQ(sink.writes[0][3], 0xbf);
  CHECK_EQ(sink.writes[0][4], 0x22);
}

TEST(engine_set_pump_enqueues_two_toggles_from_zero) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  // status with pump1 = 0; mark config so engine doesn't also enqueue requests
  feed_hex(e, "7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 00 00 02 03 "
              "00 00 00 00 00 66 00 00 00 9b 7e");
  // pretend config known so auto-request is quiet: feed config2 + info + filter
  feed_hex(e, "7e 1a 0a bf 24 64 dc 11 00 42 46 42 50 32 30 20 20 01 3d 12 38 2e 01 0a 04 00 9b 7e");
  feed_hex(e, "7e 0b 0a bf 2e 0a 00 01 d0 00 44 6f 7e");
  feed_hex(e, "7e 0d 0a bf 23 08 00 02 00 94 00 01 1e d0 7e");
  size_t before = sink.writes.size();
  e.set_pump(0, 2);  // 0 -> 2 needs 2 toggles (max speed 2 from config)
  for (int i = 0; i < 6; i++) feed_hex(e, "7e 05 10 bf 06 5c 7e");
  // Count all toggle frames with type 0x11 to verify toggles are being sent
  size_t toggles = 0;
  for (size_t i = before; i < sink.writes.size(); i++)
    if (sink.writes[i][4] == 0x11) toggles++;
  CHECK_EQ(toggles, 2);
}
