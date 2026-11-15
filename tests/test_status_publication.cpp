#include "test_framework.h"
#include "protocol_engine.h"
#include "status_publication.h"
#include <vector>
using namespace esphome::balboa_spa;

TEST(publication_changed_jets_and_light_notify_without_wait_or_uart_tx) {
  ProtocolEngine engine;
  engine.set_read_only(true);
  unsigned writes = 0;
  engine.set_write_fn([&](const uint8_t *, size_t) { writes++; });
  StatusPublication publication;
  uint64_t now = 0;
  std::vector<uint64_t> times;
  std::vector<SpaStatus> observed;
  engine.on_status_update = [&]() {
    if (publication.observe(engine.status(), now)) {
      times.push_back(now); observed.push_back(engine.status());
    }
  };
  auto feed_status = [&](uint8_t pump, bool light) {
    uint8_t payload[24] = {};
    payload[2] = 100; payload[11] = pump; payload[14] = light ? 3 : 0;
    payload[20] = 102;
    uint8_t frame[40];
    size_t n = build_frame(frame, 0xff, msg::STATUS0, msg::STATUS1, payload, sizeof(payload));
    engine.feed(frame, n);
  };
  feed_status(0, false);
  for (unsigned i = 1; i <= 96; i++) { now = i * 10; feed_status(0, false); }
  CHECK_EQ(times.size(), 1);
  now = 970; feed_status(1, false);
  CHECK_EQ(times.size(), 2); CHECK_EQ(times.back(), now);
  CHECK_EQ(observed.back().pumps[0], 1);
  now = 980; feed_status(1, true);
  CHECK_EQ(times.size(), 3); CHECK_EQ(times.back(), now);
  CHECK(observed.back().lights[0]);
  engine.set_light(0, false); // command receipt cannot alter observed publication
  CHECK_EQ(observed.back().lights[0], true);
  CHECK(engine.status().lights[0]);
  now = 990; feed_status(1, true);
  CHECK_EQ(times.size(), 3);
  now = 1000; feed_status(0, false);
  CHECK_EQ(times.size(), 4);
  CHECK_EQ(observed.back().pumps[0], 0); CHECK(!observed.back().lights[0]);
  CHECK_EQ(writes, 0);
}

TEST(publication_unchanged_status_refreshes_every_thirty_seconds) {
  StatusPublication p; SpaStatus s; s.valid = true;
  CHECK(p.observe(s, 0));
  CHECK(!p.observe(s, 29999));
  CHECK_EQ(p.observed_ms(), 29999);
  CHECK(p.observe(s, 30000));
}

TEST(publication_ignores_invalid_status_and_tracks_latest_valid_observation) {
  StatusPublication p; SpaStatus s;
  CHECK(!p.observe(s, 0));
  CHECK(!p.have_observation());
  s.valid = true; CHECK(p.observe(s, 0));
  CHECK(p.have_observation());
  CHECK_EQ(p.observed_ms(), 0);
  CHECK_EQ(p.observation_seq(), 1);
  CHECK(!p.observe(s, 100));
  CHECK_EQ(p.observed_ms(), 100);
  CHECK_EQ(p.observation_seq(), 2);
}

TEST(publication_preserves_unchanged_entity_refresh_and_64_bit_uptime) {
  StatusPublication p; SpaStatus s; s.valid = true;
  uint64_t start = 4294967200ULL;
  CHECK(p.observe(s, start));
  CHECK(!p.observe(s, start + 29999));
  CHECK(p.observe(s, start + 30000));
  CHECK(!p.observe(s, start + 30001));
  s.pumps[5] = 1; CHECK(p.observe(s, start + 30002));
  s.lights[1] = true; CHECK(p.observe(s, start + 30003));
}
