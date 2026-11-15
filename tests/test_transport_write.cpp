#include "test_framework.h"
#include "transport_write.h"
#include "protocol_engine.h"
#include <limits>
using namespace esphome::balboa_spa;

struct FakeTransport {
  TransportWrite transport;
  size_t space{512};
  bool healthy{true}, flush_ok{true}, fail_on_write{false}, driving{false};
  unsigned writes{0}, flushes{0}, enables{0}, disables{0};
  uint32_t timeout{0};
  bool send(size_t size = 10) {
    return transport.send(size, [&]() { return space; }, [&]() { return healthy; },
        [&](bool drive) { driving = drive; if (drive) enables++; else disables++; },
        [&]() { CHECK(driving); writes++; if (fail_on_write) healthy = false; },
        [&](uint32_t bound) { CHECK(driving); flushes++; timeout = bound; return flush_ok; });
  }
};

TEST(transport_full_buffer_and_unknown_capacity_never_enable_driver_or_write) {
  for (size_t space : {size_t(0), size_t(9), std::numeric_limits<size_t>::max()}) {
    FakeTransport io; io.space = space;
    CHECK(!io.send()); CHECK(!io.driving); CHECK_EQ(io.writes, 0);
    CHECK_EQ(io.enables, 0); CHECK_EQ(io.flushes, 0); CHECK(io.transport.faulted());
    io.space = 512;
    CHECK(!io.send()); CHECK_EQ(io.writes, 0); // no late retry in a later poll
  }
}

TEST(transport_failed_drain_releases_driver_and_latches_against_late_tail_transmission) {
  FakeTransport io; io.flush_ok = false;
  CHECK(!io.send()); CHECK(!io.driving); CHECK_EQ(io.disables, 1);
  CHECK_EQ(io.timeout, TransportWrite::FLUSH_TIMEOUT_MS); CHECK(io.transport.faulted());
  io.flush_ok = true;
  CHECK(!io.send()); CHECK_EQ(io.writes, 1); CHECK_EQ(io.enables, 1);
}

TEST(transport_short_or_failed_write_releases_driver_without_waiting_and_cannot_retry) {
  FakeTransport io; io.fail_on_write = true;
  CHECK(!io.send()); CHECK(!io.driving); CHECK_EQ(io.flushes, 0);
  CHECK(io.transport.faulted()); CHECK_EQ(io.disables, 1);
  io.healthy = true; io.fail_on_write = false;
  CHECK(!io.send()); CHECK_EQ(io.writes, 1);
}

TEST(transport_healthy_registered_replies_remain_repeatable) {
  FakeTransport io;
  for (unsigned n = 0; n < 20; n++) { CHECK(io.send(7)); CHECK(!io.driving); }
  CHECK_EQ(io.writes, 20); CHECK_EQ(io.flushes, 20); CHECK(!io.transport.faulted());
}

TEST(transport_fault_revokes_pending_registration_and_never_adopts_a_late_assignment) {
  ProtocolEngine engine; FakeTransport io; io.flush_ok = false;
  engine.set_read_only(false); engine.set_registration_only(true);
  engine.set_checked_write_fn([&](const uint8_t *, size_t n) { return io.send(n); });
  uint8_t frame[32]; size_t n = build_frame(frame, 0xfe, 0xbf, 0x00, nullptr, 0);
  engine.feed(frame, n); CHECK_EQ(io.writes, 1); CHECK(!engine.registered());
  io.flush_ok = true;
  engine.feed(frame, n); CHECK_EQ(io.writes, 1);
  uint8_t assigned[] = {0x16, ID_REQUEST_HASH0, ID_REQUEST_HASH1};
  n = build_frame(frame, 0xfe, 0xbf, 0x02, assigned, sizeof(assigned));
  engine.feed(frame, n); CHECK(!engine.registered()); CHECK_EQ(io.writes, 1);
}
