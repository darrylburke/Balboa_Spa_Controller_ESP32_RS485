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

TEST(events_full_sample_storage_coalesces_new_polls_without_consuming_sequence) {
  EventRecorder r; r.set_enabled(true);
  r.record(EventKind::REQUEST);
  for (size_t i = 0; i < EventRecorder::SAMPLE_CAP; i++)
    r.record(EventKind::READY, 0x10, 0xbf06, 0, 0, true);
  CHECK(!r.sample_poll(0x11));
  CHECK_EQ(r.coalesced(), 1); CHECK_EQ(r.sample_lost(), 0); CHECK_EQ(r.lost(), 0);
  CHECK_EQ(r.record(EventKind::ACCEPTED), EventRecorder::SAMPLE_CAP + 2);
}

TEST(events_reserve_complete_idle_sample_chains_and_resume_after_drain) {
  EventRecorder r; r.set_enabled(true);
  for (size_t i = 0; i < EventRecorder::SAMPLE_CAP - 1; i++)
    r.record(EventKind::READY, 0x10, 0xbf06, 0, 0, true);
  CHECK(!r.sample_poll(0x11, 3));
  BusEvent e; CHECK(r.pop(e)); CHECK(r.pop(e));
  CHECK(r.sample_poll(0x11, 3));
  auto ref = r.record(EventKind::READY, 0x11, 0xbf06, 0, 0, true);
  r.record(EventKind::UART_RETURN, 0x11, 0xbf07, 7, ref, true);
  r.record(EventKind::WRITE_RETURN, 0x11, 0xbf07, 1, ref, true);
  CHECK_EQ(r.sample_lost(), 0); CHECK_EQ(r.pending(), EventRecorder::SAMPLE_CAP);
  CHECK_EQ(r.coalesced(), 1);
}

TEST(events_external_frame_sampling_has_its_own_disclosed_omission_counter) {
  EventRecorder r; r.set_enabled(true); EventTime t{0,0}; r.set_clock([&]() { return t; });
  CHECK(r.sample_other_frame(0x10)); CHECK(!r.sample_other_frame(0x10));
  CHECK(r.sample_other_frame(0x11)); CHECK_EQ(r.other_frames_coalesced(), 1);
  CHECK_EQ(r.coalesced(), 0);
  t.uptime_ms = 5000; CHECK(r.sample_other_frame(0x10));
}

TEST(events_many_other_channels_leave_room_for_our_complete_idle_chain) {
  EventRecorder r; r.set_enabled(true);
  for (uint8_t ch = 0x10; ch <= 0x25; ch++)
    if (r.sample_poll(ch)) r.record(EventKind::READY, ch, 0xbf06, 0, 0, true);
  CHECK_EQ(r.pending(), EventRecorder::SAMPLE_CAP - EventRecorder::IDLE_SAMPLE_SLOTS);
  CHECK(!r.sample_other_frame(0x10));
  CHECK(r.sample_poll(0x26, EventRecorder::IDLE_SAMPLE_SLOTS));
  auto ref = r.record(EventKind::READY, 0x26, 0xbf06, 0, 0, true);
  r.record(EventKind::UART_RETURN, 0x26, 0xbf07, 7, ref, true);
  r.record(EventKind::WRITE_RETURN, 0x26, 0xbf07, 1, ref, true);
  CHECK_EQ(r.pending(), EventRecorder::SAMPLE_CAP); CHECK_EQ(r.sample_lost(), 0);
}

TEST(events_offline_publication_does_not_build_or_consume_the_registration_batch) {
  EventRecorder r; r.set_enabled(true);
  auto request = r.record(EventKind::REQUEST, 0xfe, 0xbf01);
  r.record(EventKind::ACK_RETURN, 0x27, 0xbf03, 1, request);
  r.record(EventKind::ACCEPTED, 0x27, 0xbf02, 0, request);
  unsigned builds = 0;
  for (unsigned i = 0; i < 120; i++)
    CHECK(!r.publish_batch(false, [&](const BusEvent *, size_t) { builds++; return true; }));
  CHECK_EQ(builds, 0); CHECK_EQ(r.pending(), 3);
  CHECK(r.publish_batch(true, [&](const BusEvent *batch, size_t count) {
    CHECK_EQ(count, 3); CHECK_EQ(batch[0].seq, request); CHECK_EQ(batch[2].kind, EventKind::ACCEPTED);
    CHECK_EQ(r.pending(), 3); return true;
  }));
  CHECK_EQ(r.pending(), 0);
}

TEST(events_rejected_outbound_batch_is_retained_for_identical_later_attempt) {
  EventRecorder r; r.set_enabled(true);
  auto request = r.record(EventKind::REQUEST, 0xfe, 0xbf01);
  CHECK(!r.publish_batch(true, [&](const BusEvent *batch, size_t count) {
    CHECK_EQ(count, 1); CHECK_EQ(batch[0].seq, request); return false;
  }));
  CHECK_EQ(r.pending(), 1);
  CHECK(r.publish_batch(true, [&](const BusEvent *batch, size_t count) {
    CHECK_EQ(count, 1); CHECK_EQ(batch[0].seq, request); return true;
  }));
  CHECK_EQ(r.pending(), 0);
}

TEST(events_batch_commit_preserves_new_records_and_refuses_an_evicted_prefix) {
  EventRecorder r; r.set_enabled(true); auto first = r.record(EventKind::REQUEST);
  CHECK(r.publish_batch(true, [&](const BusEvent *batch, size_t count) {
    CHECK_EQ(count, 1); CHECK_EQ(batch[0].seq, first);
    r.record(EventKind::ACCEPTED); return true;
  }));
  CHECK_EQ(r.pending(), 1);
  BusEvent e; CHECK(r.pop(e)); CHECK_EQ(e.kind, EventKind::ACCEPTED);
  r.record(EventKind::REQUEST);
  CHECK(!r.publish_batch(true, [&](const BusEvent *, size_t) {
    for (unsigned i = 0; i < 100; i++) r.record(EventKind::REJECTED);
    return true;
  }));
  CHECK_EQ(r.pending(), EventRecorder::CRITICAL_CAP); CHECK_EQ(r.lost(), 37);
}

TEST(events_connected_empty_batch_still_publishes_the_bounded_heartbeat) {
  EventRecorder r; r.set_enabled(true); unsigned calls = 0;
  CHECK(r.publish_batch(true, [&](const BusEvent *, size_t count) { CHECK_EQ(count, 0); calls++; return true; }));
  CHECK_EQ(calls, 1); CHECK_EQ(r.pending(), 0);
}
