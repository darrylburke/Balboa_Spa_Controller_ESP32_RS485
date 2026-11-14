#include "test_framework.h"
#include "events.h"
using namespace esphome::balboa_spa;

TEST(events_are_optional_ordered_and_timestamped_only_by_valid_clock) {
  EventRecorder r;
  CHECK_EQ(r.record(EventKind::RESTORE), 0);
  CHECK_EQ(r.pending(), 0);
  r.set_enabled(true);
  EventTime t{1234, 0};
  r.set_clock([&]() { return t; });
  CHECK_EQ(r.record(EventKind::REQUEST, 0xfe, 0xbf01), 1);
  t = {1250, 1800000000};
  CHECK_EQ(r.record(EventKind::ACK_RETURN, 0x11, 0xbf03, 1, 1), 2);
  BusEvent e;
  CHECK(r.pop(e)); CHECK_EQ(e.time.uptime_ms, 1234); CHECK_EQ(e.time.utc_s, 0);
  CHECK(r.pop(e)); CHECK_EQ(e.time.utc_s, 1800000000); CHECK_EQ(e.ref, 1);
  CHECK(!r.pop(e));
}

TEST(events_bound_overflow_and_separate_poll_samples_from_critical_evidence) {
  EventRecorder r; r.set_enabled(true);
  r.record(EventKind::REQUEST);
  for (int i = 0; i < 100; i++) r.record(EventKind::READY, 0x10, 0xbf06, 0, 0, true);
  CHECK_EQ(r.pending(), 17);
  CHECK_EQ(r.sample_lost(), 84);
  CHECK_EQ(r.lost(), 0);
  BusEvent e;
  CHECK(r.pop(e)); CHECK_EQ(e.kind, EventKind::REQUEST);
  for (int i = 0; i < 100; i++) r.record(EventKind::REJECTED);
  CHECK_EQ(r.lost(), 36);
  CHECK_EQ(r.pending(), 80);
}

TEST(events_poll_sampling_is_per_channel_and_survives_millis_rollover) {
  EventRecorder r; r.set_enabled(true);
  EventTime t{4294967290ULL, 0}; r.set_clock([&]() { return t; });
  CHECK(r.sample_poll(0x10)); CHECK(r.sample_poll(0x11));
  CHECK(!r.sample_poll(0x10));
  t.uptime_ms += 5000;
  CHECK(r.sample_poll(0x10));
  CHECK_EQ(r.coalesced(), 1);
}
