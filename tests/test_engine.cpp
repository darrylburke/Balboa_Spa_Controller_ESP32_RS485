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

// Bus fixtures. Channel 0x10 stands in for the client that already owns the bus
// on the real spa; we are assigned 0x11 by the handshake.
#define STATUS_FRAME "7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 01 00 02 03 " \
                     "00 00 00 00 00 66 00 00 00 0f 7e"
#define CTS          "7e 05 fe bf 00 ac 7e"        // fe bf 00 "any new clients?"
#define ASSIGN_11    "7e 08 fe bf 02 11 f1 73 9c 7e"     // fe bf 02 11 -> our channel is 0x11
#define READY_OURS   "7e 05 11 bf 06 37 7e"        // Ready addressed to 0x11
#define READY_OTHER  "7e 05 10 bf 06 5c 7e"        // Ready addressed to 0x10 (not ours)

// Full assignment responses carry channel + the two request correlation bytes.
static void assign_channel(ProtocolEngine &e, uint8_t id, uint8_t hash0 = 0xf1,
                           uint8_t hash1 = 0x73) {
  uint8_t frame[10];
  const uint8_t payload[] = {id, hash0, hash1};
  size_t n = build_frame(frame, channel::BROADCAST, msg::ID_ASSIGN0,
                        msg::ID_ASSIGN1, payload, sizeof(payload));
  e.feed(frame, n);
}

// Complete the join handshake so the engine is allowed to transmit.
static void register_engine(ProtocolEngine &e) {
  e.set_write_fn([](const uint8_t *, size_t) {});
  feed_hex(e, CTS);
  feed_hex(e, ASSIGN_11);
}

TEST(engine_decodes_status_and_fires_callback) {
  ProtocolEngine e;
  int fired = 0;
  e.on_status_update = [&] { fired++; };
  feed_hex(e, STATUS_FRAME);
  CHECK(e.status().valid);
  CHECK_EQ(e.status().current_temp_raw, 100);
  CHECK_EQ(fired, 1);
}

// --- channel negotiation ---

TEST(engine_starts_unregistered_and_silent) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  CHECK(!e.registered());
  CHECK_EQ(e.channel(), channel::UNASSIGNED);
  feed_hex(e, STATUS_FRAME);
  e.toggle_item(item::LIGHT1);
  // Readys for somebody else must never make us transmit.
  for (int i = 0; i < 5; i++) feed_hex(e, READY_OTHER);
  CHECK_EQ(sink.writes.size(), 0);
}

TEST(engine_requests_channel_on_cts) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  feed_hex(e, CTS);
  CHECK_EQ(sink.writes.size(), 1);
  auto expect = hexb("7e 08 fe bf 01 02 f1 73 b9 7e");  // verified against the real spa
  CHECK(sink.writes[0] == expect);
}

TEST(engine_adopts_assigned_channel_and_acks) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  feed_hex(e, CTS);
  feed_hex(e, ASSIGN_11);
  CHECK(e.registered());
  CHECK_EQ(e.channel(), 0x11);
  CHECK_EQ(sink.writes.size(), 2);                      // ID request + ID ack
  auto ack = hexb("7e 05 11 bf 03 2c 7e");
  CHECK(sink.writes[1] == ack);
}

TEST(engine_rejects_out_of_range_channel) {
  ProtocolEngine e;
  e.set_write_fn([](const uint8_t *, size_t) {});
  feed_hex(e, CTS);
  assign_channel(e, 0x9c);
  CHECK(!e.registered());
  CHECK_EQ(e.channel(), channel::UNASSIGNED);
}

TEST(engine_ignores_further_assignments_once_registered) {
  ProtocolEngine e;
  register_engine(e);
  CHECK_EQ(e.channel(), 0x11);
  assign_channel(e, 0x15);  // a later assignment must not steal us
  CHECK_EQ(e.channel(), 0x11);
}

TEST(engine_stays_silent_in_another_clients_window) {
  ProtocolEngine e;
  Sink sink;
  register_engine(e);
  feed_hex(e, STATUS_FRAME);
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });  // attach after handshake
  e.toggle_item(item::LIGHT1);
  for (int i = 0; i < 10; i++) feed_hex(e, READY_OTHER);
  CHECK_EQ(sink.writes.size(), 0);   // never transmit in 0x10's slot
  feed_hex(e, READY_OURS);
  CHECK(sink.writes.size() > 0);     // but do transmit in our own
}

TEST(engine_answers_our_window_with_nothing_to_send_when_idle) {
  ProtocolEngine e;
  Sink sink;
  register_engine(e);
  // Config must be complete BEFORE the Status arrives, otherwise the engine
  // auto-queues config requests and our window is used for those instead.
  feed_hex(e, "7e 1a 0a bf 24 64 dc 11 00 42 46 42 50 32 30 20 20 01 3d 12 38 2e 01 0a 04 00 9b 7e");
  feed_hex(e, "7e 0b 0a bf 2e 0a 00 01 d0 00 44 6f 7e");
  feed_hex(e, "7e 0d 0a bf 23 08 00 02 00 94 00 01 1e d0 7e");
  feed_hex(e, STATUS_FRAME);
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 1);
  auto nts = hexb("7e 05 11 bf 07 30 7e");
  CHECK(sink.writes[0] == nts);
}

// --- channel persistence / resume ---

TEST(engine_reports_assigned_channel_for_persistence) {
  ProtocolEngine e;
  uint8_t saved = 0;
  int calls = 0;
  e.on_channel_assigned = [&](uint8_t ch) { saved = ch; calls++; };
  register_engine(e);
  CHECK_EQ(saved, 0x11);
  CHECK_EQ(calls, 1);
}

TEST(engine_restored_channel_is_observation_only_until_fresh_registration) {
  ProtocolEngine e;
  Sink sink;
  int assigned_calls = 0;
  e.on_channel_assigned = [&](uint8_t) { assigned_calls++; };
  e.restore_channel(0x10);
  CHECK(!e.registered());
  CHECK_EQ(e.channel(), 0x10);
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  e.toggle_item(item::LIGHT1);
  for (int i = 0; i < 40; i++) {
    feed_hex(e, STATUS_FRAME);
    feed_hex(e, READY_OTHER);
  }
  CHECK(!e.registered());
  CHECK_EQ(sink.writes.size(), 0);
  CHECK_EQ(assigned_calls, 0);
  feed_hex(e, CTS);
  CHECK_EQ(sink.writes.size(), 1);
  feed_hex(e, ASSIGN_11);
  CHECK(e.registered());
  CHECK_EQ(e.channel(), 0x11);
  CHECK_EQ(assigned_calls, 1);
  CHECK_EQ(sink.writes.size(), 2);
  feed_hex(e, READY_OTHER);
  CHECK_EQ(sink.writes.size(), 2);
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 3);
  CHECK(sink.writes.back() == hexb("7e 05 11 bf 07 30 7e"));
}

TEST(engine_restore_rejects_invalid_channels) {
  ProtocolEngine e;
  e.restore_channel(channel::UNASSIGNED);
  CHECK(!e.registered());
  e.restore_channel(0x9c);                 // above MAX
  CHECK(!e.registered());
  e.restore_channel(0x11);
  CHECK_EQ(e.channel(), 0x11);
}

TEST(engine_releases_stale_channel_and_rejoins) {
  ProtocolEngine e;
  Sink sink;
  int stale = 0;
  e.on_channel_stale = [&] { stale++; };
  register_engine(e);                     // legitimately registered, then forgotten
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  // Statuses keep coming but no window is ever offered to 0x11.
  for (int i = 0; i < 30; i++) feed_hex(e, STATUS_FRAME);
  CHECK(!e.registered());
  CHECK_EQ(stale, 1);
  // Now a CTS must start a fresh join.
  feed_hex(e, CTS);
  auto idreq = hexb("7e 08 fe bf 01 02 f1 73 b9 7e");
  CHECK(sink.writes.size() > 0);
  CHECK(sink.writes.back() == idreq);
}

TEST(engine_keeps_channel_while_still_polled) {
  ProtocolEngine e;
  register_engine(e);
  // Being polled resets the staleness counter, so we hold the channel.
  for (int i = 0; i < 100; i++) {
    feed_hex(e, STATUS_FRAME);
    if (i % 5 == 0) feed_hex(e, READY_OURS);
  }
  CHECK(e.registered());
  CHECK_EQ(e.channel(), 0x11);
}

// --- bounded config retry ---

TEST(engine_retries_missing_config_but_gives_up) {
  ProtocolEngine e;
  Sink sink;
  register_engine(e);
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  // Feed many Statuses with no config ever answering, draining our windows.
  for (int i = 0; i < 400; i++) {
    feed_hex(e, STATUS_FRAME);
    feed_hex(e, READY_OURS);
  }
  CHECK(e.gave_up_on_config());
  CHECK_EQ(e.config_attempts(), 20);       // CONFIG_MAX_ATTEMPTS
  // Count actual bf 22 requests: bounded, not one per status.
  size_t reqs = 0;
  for (auto &w : sink.writes) if (w.size() > 4 && w[4] == 0x22) reqs++;
  CHECK(reqs <= 60);                       // 20 attempts x 3 missing types
  CHECK(reqs > 0);
}

TEST(engine_config_progress_resets_retry_budget) {
  ProtocolEngine e;
  register_engine(e);
  for (int i = 0; i < 40; i++) { feed_hex(e, STATUS_FRAME); feed_hex(e, READY_OURS); }
  CHECK(e.config_attempts() > 0);
  // A config response arriving is progress -> budget resets.
  feed_hex(e, "7e 1a 0a bf 24 64 dc 11 00 42 46 42 50 32 30 20 20 01 3d 12 38 2e 01 0a 04 00 9b 7e");
  CHECK_EQ(e.config_attempts(), 0);
}

TEST(engine_stops_requesting_once_config_complete) {
  ProtocolEngine e;
  Sink sink;
  register_engine(e);
  feed_hex(e, "7e 1a 0a bf 24 64 dc 11 00 42 46 42 50 32 30 20 20 01 3d 12 38 2e 01 0a 04 00 9b 7e");
  feed_hex(e, "7e 0b 0a bf 2e 0a 00 01 d0 00 44 6f 7e");
  feed_hex(e, "7e 0d 0a bf 23 08 00 02 00 94 00 01 1e d0 7e");
  feed_hex(e, STATUS_FRAME);
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  for (int i = 0; i < 30; i++) { feed_hex(e, STATUS_FRAME); feed_hex(e, READY_OURS); }
  size_t reqs = 0;
  for (auto &w : sink.writes) if (w.size() > 4 && w[4] == 0x22) reqs++;
  CHECK_EQ(reqs, 0);                       // nothing missing -> never asks
}

// --- transmission, once registered ---

TEST(engine_sends_queued_command_only_on_our_ready) {
  ProtocolEngine e;
  Sink sink;
  register_engine(e);
  feed_hex(e, STATUS_FRAME);   // TX also requires a Status first
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  e.toggle_item(item::LIGHT1);
  CHECK_EQ(sink.writes.size(), 0);  // nothing sent yet (no Ready for us)
  // Incomplete config auto-queues 3 requests, then the toggle = 4 total.
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 1);
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 2);
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 3);
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 4);
  auto expect = hexb("7e 07 11 bf 11 11 00 1e 7e");   // toggle, sent as src 0x11
  CHECK(sink.writes[3] == expect);
}

TEST(engine_read_only_never_requests_or_transmits) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  e.set_read_only(true);
  feed_hex(e, CTS);
  CHECK(!e.registered());            // read-only must not request registration
  feed_hex(e, STATUS_FRAME);
  e.toggle_item(item::LIGHT1);
  feed_hex(e, READY_OURS);
  feed_hex(e, READY_OTHER);
  CHECK_EQ(sink.writes.size(), 0);
}

TEST(engine_no_tx_before_first_status) {
  ProtocolEngine e;
  Sink sink;
  register_engine(e);
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  e.toggle_item(item::LIGHT1);
  feed_hex(e, READY_OURS);           // our window, but no Status seen yet
  // Only the idle nothing-to-send may go out; the queued command must not.
  CHECK_EQ(sink.writes.size(), 1);
  CHECK(sink.writes.back() == hexb("7e 05 11 bf 07 30 7e"));
}

TEST(engine_requests_config_after_status_when_incomplete) {
  ProtocolEngine e;
  Sink sink;
  register_engine(e);
  CHECK_EQ(e.channel(), 0x11);
  feed_hex(e, STATUS_FRAME);   // status only -> config incomplete
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  feed_hex(e, READY_OURS);
  feed_hex(e, READY_OURS);
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 3);
  CHECK_EQ(sink.writes[0][2], 0x11);   // sent as our assigned channel
  CHECK_EQ(sink.writes[0][3], 0xbf);
  CHECK_EQ(sink.writes[0][4], 0x22);
}

TEST(engine_set_pump_enqueues_two_toggles_from_zero) {
  ProtocolEngine e;
  Sink sink;
  register_engine(e);
  // status with pump1 = 0; mark config so engine doesn't also enqueue requests
  feed_hex(e, "7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 00 00 02 03 "
              "00 00 00 00 00 66 00 00 00 9b 7e");
  feed_hex(e, "7e 1a 0a bf 24 64 dc 11 00 42 46 42 50 32 30 20 20 01 3d 12 38 2e 01 0a 04 00 9b 7e");
  feed_hex(e, "7e 0b 0a bf 2e 0a 00 01 d0 00 44 6f 7e");
  feed_hex(e, "7e 0d 0a bf 23 08 00 02 00 94 00 01 1e d0 7e");
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  e.set_pump(0, 2);  // 0 -> 2 needs 2 toggles (max speed 2 from config)
  for (int i = 0; i < 6; i++) feed_hex(e, READY_OURS);
  size_t toggles = 0;
  for (size_t i = 0; i < sink.writes.size(); i++)
    if (sink.writes[i][4] == 0x11 && sink.writes[i][5] == item::PUMP1) toggles++;
  CHECK_EQ(toggles, 2);
}

TEST(engine_passive_diagnostics_observe_assignments_and_all_windows_without_tx) {
  ProtocolEngine e;
  Sink sink;
  e.set_read_only(true);
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  feed_hex(e, CTS);
  feed_hex(e, ASSIGN_11);
  feed_hex(e, READY_OTHER);
  feed_hex(e, READY_OURS);
  feed_hex(e, ASSIGN_11);  // visible even though passive
  const auto &d = e.diagnostics();
  CHECK_EQ(d.new_client_cts, 1);
  CHECK_EQ(d.assignments, 2);
  CHECK_EQ(d.last_assignment, 0x11);
  CHECK_EQ(d.channel_adoptions, 0);
  CHECK_EQ(d.ready, 2);
  CHECK_EQ(d.ready_ours, 0);
  CHECK_EQ(d.ready_by_channel[0x10], 1);
  CHECK_EQ(d.ready_by_channel[0x11], 1);
  CHECK_EQ(d.last_ready_channel, 0x11);
  CHECK_EQ(sink.writes.size(), 0);
}

TEST(engine_passive_restore_does_not_claim_or_expire_ownership) {
  ProtocolEngine e;
  e.set_read_only(true);
  e.restore_channel(0x9c);
  e.restore_channel(0x11);
  CHECK_EQ(e.diagnostics().channel_restores, 1);
  for (int i = 0; i < 30; i++) feed_hex(e, STATUS_FRAME);
  CHECK_EQ(e.diagnostics().channel_stale, 0);
  CHECK(!e.registered());
}

TEST(engine_passive_diagnostics_separate_observation_from_adoption) {
  ProtocolEngine e;
  Sink sink;
  e.set_read_only(true);
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  feed_hex(e, ASSIGN_11);
  // Observe another client's assignment without adopting either ID.
  uint8_t assignment[8];
  const uint8_t assigned_channel = 0x15;
  size_t n = build_frame(assignment, channel::BROADCAST, msg::ID_ASSIGN0,
                        msg::ID_ASSIGN1, &assigned_channel, 1);
  e.feed(assignment, n);
  CHECK_EQ(e.diagnostics().assignments, 2);
  CHECK_EQ(e.diagnostics().last_assignment, 0x15);
  CHECK_EQ(e.diagnostics().channel_adoptions, 0);
  CHECK_EQ(e.channel(), channel::UNASSIGNED);
  feed_hex(e, STATUS_FRAME);
  e.toggle_item(item::LIGHT1);
  feed_hex(e, READY_OURS);
  CHECK_EQ(e.diagnostics().ready_ours, 0);
  CHECK_EQ(sink.writes.size(), 0);  // neither handshake, idle reply nor queue writes
}

TEST(engine_diagnostics_poll_map_handles_all_byte_channels) {
  ProtocolEngine e;
  e.set_read_only(true);
  e.restore_channel(0x11);
  for (unsigned ch = 0; ch < 256; ch++) {
    uint8_t frame[7];
    size_t n = build_frame(frame, ch, msg::READY0, msg::READY1, nullptr, 0);
    e.feed(frame, n);
  }
  CHECK_EQ(e.diagnostics().ready, 256);
  CHECK_EQ(e.diagnostics().ready_ours, 1);
  CHECK_EQ(e.diagnostics().last_ready_channel, 255);
  for (unsigned ch = 0; ch <= channel::MAX; ch++)
    CHECK_EQ(e.diagnostics().ready_by_channel[ch], 1);
}

TEST(engine_diagnostics_ignore_bad_crc_assignment) {
  ProtocolEngine e;
  e.set_read_only(true);
  feed_hex(e, "7e 06 fe bf 02 11 bb 7e");
  CHECK_EQ(e.diagnostics().assignments, 0);
  CHECK_EQ(e.diagnostics().channel_adoptions, 0);
  CHECK(!e.registered());
}

TEST(engine_rejects_unsolicited_and_unrelated_assignments) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  assign_channel(e, 0x11);  // no pending transmitted request
  CHECK(!e.registered());
  CHECK_EQ(sink.writes.size(), 0);
  feed_hex(e, CTS);
  assign_channel(e, 0x10, 0x76, 0x57);  // another client's correlation bytes
  CHECK(!e.registered());
  CHECK_EQ(sink.writes.size(), 1);
  feed_hex(e, READY_OTHER);
  CHECK_EQ(sink.writes.size(), 1);
  assign_channel(e, 0x11);
  CHECK(e.registered());
  CHECK_EQ(sink.writes.size(), 2);
  CHECK(sink.writes.back() == hexb("7e 05 11 bf 03 2c 7e"));
  assign_channel(e, 0x12);  // a duplicate/later assignment cannot steal ownership
  CHECK_EQ(e.channel(), 0x11);
  CHECK_EQ(sink.writes.size(), 2);
}

TEST(engine_requires_full_valid_assignment_and_transmitted_request) {
  ProtocolEngine e;
  feed_hex(e, CTS);  // no UART callback: no request was sent
  assign_channel(e, 0x11);
  CHECK(!e.registered());
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  assign_channel(e, 0x11);
  CHECK(!e.registered());
  feed_hex(e, CTS);
  feed_hex(e, "7e 06 fe bf 02 11 ba 7e");  // old shortened fixture: no correlation
  CHECK(!e.registered());
  for (uint8_t id : {0x00, 0x0a, 0x30, 0x9c, 0xfe}) {
    assign_channel(e, id);
    CHECK(!e.registered());
  }
  CHECK_EQ(sink.writes.size(), 1);
  assign_channel(e, 0x11, 0xf1, 0x93);  // documented ambiguous non-echo: fail closed
  CHECK(!e.registered());
  assign_channel(e, 0x11);
  CHECK(e.registered());
  CHECK_EQ(sink.writes.size(), 2);
}

TEST(engine_channel_loss_discards_queued_frames_before_reassignment) {
  ProtocolEngine e;
  register_engine(e);
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  feed_hex(e, STATUS_FRAME);  // queues config requests under 0x11
  e.toggle_item(item::LIGHT1);
  for (int i = 0; i < 29; i++) feed_hex(e, STATUS_FRAME);
  CHECK(!e.registered());
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 0);
  e.toggle_item(item::PUMP1);  // must not survive the unowned interval either
  feed_hex(e, CTS);
  assign_channel(e, 0x12);
  CHECK(e.registered());
  uint8_t ready[7];
  size_t n = build_frame(ready, 0x12, msg::READY0, msg::READY1, nullptr, 0);
  e.feed(ready, n);
  CHECK_EQ(sink.writes.size(), 3);  // request, ack, idle reply; no old queue
  CHECK_EQ(sink.writes.back()[2], 0x12);
  CHECK_EQ(sink.writes.back()[4], msg::NOTHING1);
  e.toggle_item(item::LIGHT1);
  e.feed(ready, n);
  CHECK_EQ(sink.writes.back()[2], 0x12);
  CHECK_EQ(sink.writes.back()[4], 0x11);
  CHECK_EQ(sink.writes.back()[5], item::LIGHT1);
}

TEST(engine_restore_and_read_only_revoke_pending_and_owned_transmissions) {
  ProtocolEngine e;
  register_engine(e);
  feed_hex(e, STATUS_FRAME);
  e.toggle_item(item::LIGHT1);
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  e.restore_channel(0x10);
  CHECK(!e.registered());
  feed_hex(e, READY_OTHER);
  CHECK_EQ(sink.writes.size(), 0);
  feed_hex(e, CTS);
  e.set_read_only(true);
  assign_channel(e, 0x11);
  CHECK(!e.registered());
  CHECK_EQ(sink.writes.size(), 1);  // request only, no ack
  e.set_read_only(false);
  assign_channel(e, 0x11);  // pending request was invalidated by passive mode
  CHECK(!e.registered());
  feed_hex(e, READY_OTHER);
  CHECK_EQ(sink.writes.size(), 1);
  feed_hex(e, CTS);
  assign_channel(e, 0x11);
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 4);
  CHECK_EQ(sink.writes.back()[4], msg::NOTHING1);
}

TEST(engine_same_channel_reregistration_does_not_replay_old_commands) {
  ProtocolEngine e;
  register_engine(e);
  feed_hex(e, STATUS_FRAME);
  e.toggle_item(item::LIGHT1);
  for (int i = 0; i < 29; i++) feed_hex(e, STATUS_FRAME);
  CHECK(!e.registered());
  register_engine(e);  // controller returns the same channel this time
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 1);
  CHECK_EQ(sink.writes.back()[4], msg::NOTHING1);
}

TEST(engine_entering_read_only_discards_registered_queue_and_ownership) {
  ProtocolEngine e;
  register_engine(e);
  feed_hex(e, STATUS_FRAME);
  e.toggle_item(item::LIGHT1);
  e.set_read_only(true);
  CHECK(!e.registered());
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  feed_hex(e, CTS);
  assign_channel(e, 0x11);
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 0);
  e.set_read_only(false);
  assign_channel(e, 0x11);
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 0);
  feed_hex(e, CTS);
  assign_channel(e, 0x11);
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 3);
  CHECK_EQ(sink.writes.back()[4], msg::NOTHING1);
}

TEST(engine_competing_registration_request_cancels_our_pending_assignment) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  feed_hex(e, CTS);
  uint8_t request[10];
  const uint8_t other_client[] = {0x02, 0x76, 0x57};
  size_t n = build_frame(request, channel::BROADCAST, msg::ID_REQ0,
                        msg::ID_REQ1, other_client, sizeof(other_client));
  e.feed(request, n);
  assign_channel(e, 0x10, 0x76, 0x57);
  assign_channel(e, 0x11);  // a delayed match cannot revive the cancelled request
  CHECK(!e.registered());
  CHECK_EQ(sink.writes.size(), 1);
  feed_hex(e, READY_OTHER);
  CHECK_EQ(sink.writes.size(), 1);
  feed_hex(e, CTS);
  assign_channel(e, 0x11);
  CHECK(e.registered());
  CHECK_EQ(sink.writes.size(), 3);
}

TEST(engine_queue_rejects_foreign_address_and_corrupt_frames) {
  ProtocolEngine e;
  register_engine(e);
  // A valid Status enables queue TX. Clear automatically queued config via polls.
  feed_hex(e, STATUS_FRAME);
  for (int i = 0; i < 3; i++) feed_hex(e, READY_OURS);
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  uint8_t frame[16];
  size_t n = encode_toggle_item(frame, item::LIGHT1, 0x10);
  e.enqueue_frame(frame, n);  // stale external frame after new registration
  n = encode_toggle_item(frame, item::LIGHT1, e.channel());
  frame[n - 2] ^= 1;
  e.enqueue_frame(frame, n);
  e.enqueue_frame(frame, 2);  // truncated frame
  uint8_t prefixed[17] = {0x55};
  n = encode_toggle_item(prefixed + 1, item::LIGHT1, e.channel());
  e.enqueue_frame(prefixed, n + 1);  // parser resync must not allow wire noise
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 1);
  CHECK_EQ(sink.writes.back()[4], msg::NOTHING1);
  e.toggle_item(item::LIGHT1);
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 2);
  CHECK_EQ(sink.writes.back()[2], 0x11);
  CHECK_EQ(sink.writes.back()[4], 0x11);
}

TEST(engine_malformed_flow_control_cannot_authorize_transmission) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  uint8_t frame[8];
  const uint8_t unexpected_payload = 0x42;
  size_t n = build_frame(frame, channel::BROADCAST, msg::NEW_CLIENT0,
                        msg::NEW_CLIENT1, &unexpected_payload, 1);
  e.feed(frame, n);
  assign_channel(e, 0x11);
  CHECK(!e.registered());
  CHECK_EQ(sink.writes.size(), 0);
  feed_hex(e, CTS);
  assign_channel(e, 0x11);
  CHECK(e.registered());
  n = build_frame(frame, 0x11, msg::READY0, msg::READY1, &unexpected_payload, 1);
  e.feed(frame, n);
  CHECK_EQ(sink.writes.size(), 2);  // request and ACK only
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 3);
}

TEST(engine_passive_observer_never_competes_with_registered_topside) {
  ProtocolEngine observer;
  observer.set_read_only(true);
  observer.restore_channel(0x10);
  Sink observer_sink;
  observer.set_write_fn([&](const uint8_t *d, size_t n) { observer_sink(d, n); });
  ProtocolEngine client;
  Sink client_sink;
  client.set_write_fn([&](const uint8_t *d, size_t n) { client_sink(d, n); });
  feed_hex(client, CTS);
  assign_channel(client, 0x10);
  for (int i = 0; i < 100; i++) {
    feed_hex(observer, CTS);
    assign_channel(observer, 0x10);
    feed_hex(observer, STATUS_FRAME);
    feed_hex(observer, READY_OTHER);
    feed_hex(client, READY_OTHER);
  }
  CHECK_EQ(observer_sink.writes.size(), 0);
  CHECK(!observer.registered());
  CHECK_EQ(observer.diagnostics().assignments, 100);
  CHECK_EQ(observer.diagnostics().channel_adoptions, 0);
  CHECK_EQ(observer.diagnostics().ready_ours, 100);  // identity match, not authority
  CHECK_EQ(client_sink.writes.size(), 102);  // legitimate request, ACK, 100 idle replies
  CHECK(client.registered());
}

TEST(engine_reassignment_of_owned_channel_revokes_authority_and_queue) {
  ProtocolEngine e;
  register_engine(e);
  feed_hex(e, STATUS_FRAME);
  e.toggle_item(item::LIGHT1);
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  // A controller assignment for another hash on our channel is incompatible
  // with continued exclusive ownership, even while matching READYs continue.
  assign_channel(e, 0x11, 0x76, 0x57);
  CHECK(!e.registered());
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 0);
  feed_hex(e, CTS);
  assign_channel(e, 0x12);
  uint8_t frame[7];
  size_t n = build_frame(frame, 0x12, msg::READY0, msg::READY1, nullptr, 0);
  e.feed(frame, n);
  CHECK_EQ(sink.writes.size(), 3);
  CHECK_EQ(sink.writes.back()[4], msg::NOTHING1);
}

TEST(engine_public_queue_cannot_impersonate_registration_or_flow_control) {
  ProtocolEngine e;
  register_engine(e);
  feed_hex(e, STATUS_FRAME);
  for (int i = 0; i < 3; i++) feed_hex(e, READY_OURS);
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  uint8_t frame[16];
  for (uint8_t type : {0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x07}) {
    size_t n = build_frame(frame, e.channel(), 0xbf, type, nullptr, 0);
    e.enqueue_frame(frame, n);
  }
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 1);
  CHECK_EQ(sink.writes.back()[4], msg::NOTHING1);
}

TEST(engine_transport_removal_revokes_queue_and_authority) {
  ProtocolEngine e;
  register_engine(e);
  feed_hex(e, STATUS_FRAME);
  e.toggle_item(item::LIGHT1);
  e.set_write_fn(nullptr);
  CHECK(!e.registered());
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 0);
  feed_hex(e, CTS);
  assign_channel(e, 0x11);
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 3);
  CHECK_EQ(sink.writes.back()[4], msg::NOTHING1);
}

TEST(engine_failed_registration_writes_never_establish_ownership) {
  ProtocolEngine e;
  int calls = 0;
  e.set_checked_write_fn([&](const uint8_t *, size_t) { return ++calls != 2; });
  feed_hex(e, CTS);
  assign_channel(e, 0x11);  // request succeeded, ACK failed locally
  CHECK(!e.registered());
  CHECK_EQ(e.diagnostics().channel_adoptions, 0);
  feed_hex(e, READY_OURS);
  CHECK_EQ(calls, 2);
  e.set_checked_write_fn([](const uint8_t *, size_t) { return false; });
  feed_hex(e, CTS);
  assign_channel(e, 0x11);
  CHECK(!e.registered());
}

TEST(engine_failed_command_write_revokes_authority_without_retry) {
  ProtocolEngine e;
  register_engine(e);
  feed_hex(e, STATUS_FRAME);
  for (int i = 0; i < 3; i++) feed_hex(e, READY_OURS);
  int calls = 0;
  e.set_checked_write_fn([&](const uint8_t *, size_t) { calls++; return false; });
  e.toggle_item(item::LIGHT1);
  feed_hex(e, READY_OURS);
  CHECK(!e.registered());
  feed_hex(e, READY_OURS);
  CHECK_EQ(calls, 1);
}

TEST(engine_event_provenance_distinguishes_assignment_ack_and_acceptance) {
  ProtocolEngine e;
  e.events().set_enabled(true);
  e.events().set_clock([]() { return EventTime{1000, 0}; });
  e.set_checked_write_fn([](const uint8_t *, size_t) { return true; });
  assign_channel(e, 0x10);
  feed_hex(e, CTS);
  assign_channel(e, 0x11);
  BusEvent ev;
  uint32_t request = 0, ack = 0, accepted = 0;
  int candidates = 0, rejected = 0;
  while (e.events().pop(ev)) {
    if (ev.kind == EventKind::REQUEST) request = ev.seq;
    if (ev.kind == EventKind::ACK_RETURN) { ack = ev.seq; CHECK_EQ(ev.ref, request); }
    if (ev.kind == EventKind::ACCEPTED) { accepted = ev.seq; CHECK_EQ(ev.channel, 0x11); }
    if (ev.kind == EventKind::ASSIGNMENT) candidates++;
    if (ev.kind == EventKind::REJECTED) rejected++;
  }
  CHECK(request > 0); CHECK(ack > request); CHECK(accepted > ack);
  CHECK_EQ(candidates, 2); CHECK_EQ(rejected, 1);
}

TEST(engine_queued_frames_expire_without_replaying_toggles) {
  ProtocolEngine e;
  EventTime clock{100, 0}; e.events().set_clock([&]() { return clock; });
  register_engine(e);
  feed_hex(e, STATUS_FRAME);
  for (int i = 0; i < 3; i++) feed_hex(e, READY_OURS);
  Sink sink; e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  e.toggle_item(item::LIGHT1);
  clock.uptime_ms += 5001;
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 1);
  CHECK_EQ(sink.writes.back()[4], msg::NOTHING1);
  e.toggle_item(item::LIGHT1);
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.back()[4], 0x11);
}

TEST(engine_observed_panel_toggle_cancels_pending_same_item_only) {
  ProtocolEngine e;
  register_engine(e);
  feed_hex(e, STATUS_FRAME);
  for (int i = 0; i < 3; i++) feed_hex(e, READY_OURS);
  Sink sink; e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  e.toggle_item(item::LIGHT1);
  e.toggle_item(item::PUMP1);
  uint8_t panel[16]; size_t n = encode_toggle_item(panel, item::LIGHT1, 0x10);
  e.feed(panel, n);
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 1);
  CHECK_EQ(sink.writes.back()[5], item::PUMP1);
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.back()[4], msg::NOTHING1);
}

TEST(engine_other_client_observations_cannot_evict_registration_evidence) {
  ProtocolEngine e; e.events().set_enabled(true); register_engine(e);
  CHECK(e.registered());
  uint8_t other[16]; size_t n = encode_toggle_item(other, 0, 0x10);
  for (int i = 0; i < 200; i++) e.feed(other, n);
  CHECK_EQ(e.events().lost(), 0); CHECK_EQ(e.events().sample_lost(), 0);
  BusEvent event; bool request = false, accepted = false;
  while (e.events().pop(event)) {
    request |= event.kind == EventKind::REQUEST;
    accepted |= event.kind == EventKind::ACCEPTED;
  }
  CHECK(request); CHECK(accepted);
  CHECK_EQ(e.events().other_frames_coalesced(), 199);
}

TEST(engine_coalesced_other_client_observation_still_cancels_each_matching_item) {
  ProtocolEngine e; register_engine(e); feed_hex(e, STATUS_FRAME);
  for (int i = 0; i < 3; i++) feed_hex(e, READY_OURS);
  e.events().set_enabled(true);
  e.toggle_item(item::LIGHT1); e.toggle_item(item::PUMP1);
  uint8_t panel[16]; size_t n = encode_toggle_item(panel, item::LIGHT1, 0x10); e.feed(panel, n);
  n = encode_toggle_item(panel, item::PUMP1, 0x10); e.feed(panel, n);
  CHECK_EQ(e.events().other_frames_coalesced(), 1);
  Sink sink; e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  feed_hex(e, READY_OURS); CHECK_EQ(sink.writes.size(), 1); CHECK_EQ(sink.writes.back()[4], msg::NOTHING1);
  unsigned retired = 0; BusEvent event;
  while (e.events().pop(event)) retired += event.kind == EventKind::QUEUE_RETIRED;
  CHECK_EQ(retired, 2);
}

TEST(engine_delayed_provenance_publication_preserves_join_and_all_mandatory_replies) {
  ProtocolEngine e; e.set_registration_only(true); e.events().set_enabled(true);
  EventTime clock{0,0}; e.events().set_clock([&]() { return clock; });
  unsigned writes = 0;
  e.set_checked_write_fn([&](const uint8_t *d, size_t n) {
    writes++;
    if (e.tx_cause() != TxCause::IDLE || e.trace_write())
      e.events().record(EventKind::UART_RETURN, d[2], (uint16_t(d[3]) << 8) | d[4],
          (uint32_t(e.tx_cause()) << 24) | uint32_t(n), e.tx_ref(), e.tx_cause() == TxCause::IDLE);
    return true;
  });
  feed_hex(e, CTS); assign_channel(e, 0x27); CHECK(e.registered());
  uint8_t frame[16];
  // Three simulated minutes with no recorder drain, more other-client traffic
  // than observed on the controller, and an idle reply on every own poll.
  for (unsigned i = 0; i < 1800; i++) {
    clock.uptime_ms = i * 100;
    for (uint8_t ch = 0x10; ch <= 0x26; ch++) {
      size_t n = build_frame(frame, ch, 0xbf, 0x06, nullptr, 0); e.feed(frame, n);
    }
    size_t n = encode_toggle_item(frame, 0, 0x10); e.feed(frame, n);
    n = build_frame(frame, 0x27, 0xbf, 0x06, nullptr, 0); e.feed(frame, n);
  }
  CHECK_EQ(writes, 1802); CHECK(e.registered());
  CHECK_EQ(e.events().lost(), 0); CHECK_EQ(e.events().sample_lost(), 0);
  CHECK(e.events().coalesced() > 0); CHECK(e.events().other_frames_coalesced() > 0);
  BusEvent ev; uint32_t seq = 0; bool request = false, accepted = false, idle = false;
  while (e.events().pop(ev)) {
    CHECK_EQ(ev.seq, ++seq);
    request |= ev.kind == EventKind::REQUEST;
    accepted |= ev.kind == EventKind::ACCEPTED;
    idle |= ev.kind == EventKind::UART_RETURN && ev.frame_type == 0xbf07;
  }
  CHECK(request); CHECK(accepted); CHECK(idle);
}

// Unit-level state fixtures test the implemented arithmetic, not the BP200G1's
// unmeasured physical cycle or capability encoding.
struct CommandStateEngine : ProtocolEngine {
  void pump_observation(uint8_t speed, uint8_t levels) {
    status_.valid = true; status_.pumps[0] = speed;
    config_.valid = true; config_.pumps[0] = levels;
  }
};
TEST(engine_desired_pump_state_uses_bounded_modulo_without_retries) {
  for (uint8_t levels : {1, 2}) {
    for (uint8_t current = 0; current <= levels; current++) {
      for (uint8_t desired = 0; desired <= levels; desired++) {
        CommandStateEngine e; register_engine(e); feed_hex(e, STATUS_FRAME);
        for (int i = 0; i < 3; i++) feed_hex(e, READY_OURS);
        e.pump_observation(current, levels);
        Sink sink; e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
        e.set_pump(0, desired);
        unsigned toggles = (desired + levels + 1 - current) % (levels + 1);
        for (unsigned i = 0; i < toggles + 1; i++) feed_hex(e, READY_OURS);
        unsigned actual = 0;
        for (const auto &frame : sink.writes) if (frame[4] == 0x11) actual++;
        CHECK_EQ(actual, toggles);
        CHECK_EQ(sink.writes.back()[4], msg::NOTHING1);
      }
    }
  }
}

TEST(engine_queue_expiry_keeps_fresh_requests_and_no_automatic_reassertion) {
  ProtocolEngine e;
  EventTime clock{0, 0}; e.events().set_clock([&]() { return clock; });
  register_engine(e); feed_hex(e, STATUS_FRAME);
  for (int i = 0; i < 3; i++) feed_hex(e, READY_OURS);
  Sink sink; e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  e.toggle_item(item::LIGHT1);
  clock.uptime_ms = 4000; e.toggle_item(item::PUMP1);
  clock.uptime_ms = 5000; feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 1);
  CHECK_EQ(sink.writes.back()[5], item::PUMP1);
  clock.uptime_ms = 15000; feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.back()[4], msg::NOTHING1);
}

TEST(engine_registration_only_allows_handshake_and_idle_but_no_controls_or_config) {
  ProtocolEngine e; e.set_registration_only(true);
  Sink sink; e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  feed_hex(e, CTS); assign_channel(e, 0x11);
  CHECK(e.registered());
  for (int i = 0; i < 5; i++) {
    feed_hex(e, STATUS_FRAME);
    e.toggle_item(item::LIGHT1); e.set_target_temperature_raw(90);
    e.request_missing_config();
    feed_hex(e, READY_OURS);
  }
  CHECK_EQ(sink.writes.size(), 7);
  CHECK_EQ(sink.writes[0][4], msg::ID_REQ1);
  CHECK_EQ(sink.writes[1][4], msg::ID_ACK1);
  for (size_t i = 2; i < sink.writes.size(); i++) CHECK_EQ(sink.writes[i][4], msg::NOTHING1);
}

TEST(engine_filter_request_is_not_reported_as_observed_state_before_response) {
  ProtocolEngine e; register_engine(e);
  feed_hex(e, "7e 0d 0a bf 23 08 00 02 00 94 00 01 1e d0 7e");
  FilterCyclesData desired = e.filter_cycles();
  CHECK_EQ(desired.c1_duration_min, 120);
  desired.c1_duration_min = 180;
  e.update_filter_cycles(desired);
  CHECK_EQ(e.filter_cycles().c1_duration_min, 120);
}

TEST(engine_known_command_payload_shapes_are_checked_before_queueing) {
  ProtocolEngine e; register_engine(e); feed_hex(e, STATUS_FRAME);
  for (int i = 0; i < 3; i++) feed_hex(e, READY_OURS);
  Sink sink; e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  uint8_t frame[16]; const uint8_t short_toggle[] = {item::LIGHT1};
  size_t n = build_frame(frame, e.channel(), 0xbf, 0x11, short_toggle, 1);
  e.enqueue_frame(frame, n);
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 1);
  CHECK_EQ(sink.writes.back()[4], msg::NOTHING1);
}

TEST(engine_already_registered_assignment_is_observed_and_explicitly_rejected) {
  ProtocolEngine e; e.events().set_enabled(true);
  register_engine(e); assign_channel(e, 0x12);
  BusEvent event; bool rejected = false;
  while (e.events().pop(event))
    if (event.kind == EventKind::REJECTED && event.detail == uint32_t(RejectReason::ALREADY_REGISTERED))
      rejected = true;
  CHECK(rejected); CHECK(e.registered()); CHECK_EQ(e.channel(), 0x11);
}
