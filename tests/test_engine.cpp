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
  feed_hex(e, "7e 05 10 bf 06 5c 7e");  // Ready -> sends one
  CHECK_EQ(sink.writes.size(), 1);
  auto expect = hexb("7e 07 0a bf 11 11 00 93 7e");
  CHECK(sink.writes[0] == expect);
  feed_hex(e, "7e 05 10 bf 06 5c 7e");  // next Ready, queue empty -> nothing
  CHECK_EQ(sink.writes.size(), 1);
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
