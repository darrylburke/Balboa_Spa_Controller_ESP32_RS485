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
#define ASSIGN_11    "7e 06 fe bf 02 11 ba 7e"     // fe bf 02 11 -> our channel is 0x11
#define READY_OURS   "7e 05 11 bf 06 37 7e"        // Ready addressed to 0x11
#define READY_OTHER  "7e 05 10 bf 06 5c 7e"        // Ready addressed to 0x10 (not ours)

// Complete the join handshake so the engine is allowed to transmit.
static void register_engine(ProtocolEngine &e) {
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

TEST(engine_clamps_out_of_range_channel) {
  ProtocolEngine e;
  feed_hex(e, CTS);
  feed_hex(e, "7e 06 fe bf 02 9c 10 7e");               // 0x9c is above the valid range
  CHECK_EQ(e.channel(), channel::MAX);
}

TEST(engine_ignores_further_assignments_once_registered) {
  ProtocolEngine e;
  register_engine(e);
  CHECK_EQ(e.channel(), 0x11);
  feed_hex(e, "7e 06 fe bf 02 15 a5 7e");               // a later assignment must not steal us
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

TEST(engine_resumes_saved_channel_without_handshake) {
  ProtocolEngine e;
  Sink sink;
  int assigned_calls = 0;
  e.on_channel_assigned = [&](uint8_t) { assigned_calls++; };
  e.restore_channel(0x11);                 // as if reloaded from NVS after reboot
  CHECK(e.registered());
  CHECK_EQ(e.channel(), 0x11);
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  // A CTS must NOT trigger a new join — that is what leaks channels.
  feed_hex(e, CTS);
  CHECK_EQ(sink.writes.size(), 0);
  CHECK_EQ(assigned_calls, 0);             // restore is not an assignment
  // And we answer our restored channel's window straight away.
  feed_hex(e, READY_OURS);
  CHECK_EQ(sink.writes.size(), 1);
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
  e.restore_channel(0x11);                 // saved channel the spa has forgotten
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
  e.restore_channel(0x11);
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

TEST(engine_read_only_never_registers_or_transmits) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  e.set_read_only(true);
  feed_hex(e, CTS);
  CHECK(!e.registered());            // read-only must not even join the bus
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
  for (const auto &w : sink.writes) CHECK(w[4] != 0x11);
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
