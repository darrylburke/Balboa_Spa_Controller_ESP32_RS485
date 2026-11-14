#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace esphome {
namespace balboa_spa {

// Optional local evidence, never a protocol input. No allocations per event.
enum class EventKind : uint8_t {
  RESTORE, INVITATION, REQUEST, ASSIGNMENT, ACCEPTED, REJECTED, ACK_RETURN,
  OWNERSHIP_LOST, QUEUE_CLEARED, QUEUED, QUEUE_RETIRED, READY, WRITE_RETURN,
  UART_RETURN, QUEUE_EXPIRED, OTHER_TOGGLE
};
enum class TxCause : uint8_t { NONE, REQUEST, ACK, IDLE, QUEUE, SELFTEST };
enum class RejectReason : uint8_t {
  PASSIVE, UNOWNED, FULL, INVALID_FRAME, UNSOLICITED, CORRELATION, INVALID_CHANNEL,
  COMPETING_REQUEST, CONFLICT, TRANSPORT, MALFORMED, WRITE_FAILED, REGISTRATION_ONLY, ALREADY_REGISTERED = 32
};
struct EventTime {
  uint64_t uptime_ms = 0;
  uint32_t utc_s = 0;  // 0 means unavailable, never manufacture a pre-sync date
};
struct BusEvent {
  uint32_t seq = 0;
  EventTime time;
  EventKind kind = EventKind::RESTORE;
  uint8_t channel = 0;
  uint16_t frame_type = 0;
  uint32_t detail = 0;
  uint32_t ref = 0;
};

class EventRecorder {
 public:
  using ClockFn = std::function<EventTime()>;
  static constexpr size_t CRITICAL_CAP = 64;
  static constexpr size_t SAMPLE_CAP = 16;
  void set_enabled(bool enabled) { enabled_ = enabled; }
  bool enabled() const { return enabled_; }
  void set_clock(ClockFn clock) { clock_ = std::move(clock); }
  EventTime now() const { return clock_ ? clock_() : EventTime{}; }
  uint32_t record(EventKind kind, uint8_t ch = 0, uint16_t type = 0,
                  uint32_t detail = 0, uint32_t ref = 0, bool sampled = false) {
    if (!enabled_) return 0;
    BusEvent e{++seq_, now(), kind, ch, type, detail, ref};
    if (sampled) push_(sample_, SAMPLE_CAP, sample_head_, sample_count_, sample_lost_, e);
    else push_(critical_, CRITICAL_CAP, head_, count_, lost_, e);
    return e.seq;
  }
  // Publish a bounded batch outside UART handling. Separate storage keeps busy
  // polls/idle writes from evicting registration or command evidence.
  bool pop(BusEvent &e) {
    if (count_ == 0 && sample_count_ == 0) return false;
    bool critical = sample_count_ == 0 || (count_ != 0 &&
        int32_t(critical_[head_].seq - sample_[sample_head_].seq) < 0);
    if (critical) { e = critical_[head_]; head_ = (head_ + 1) % CRITICAL_CAP; count_--; }
    else { e = sample_[sample_head_]; sample_head_ = (sample_head_ + 1) % SAMPLE_CAP; sample_count_--; }
    return true;
  }
  uint32_t lost() const { return lost_; }
  uint32_t sample_lost() const { return sample_lost_; }
  uint32_t coalesced() const { return coalesced_; }
  size_t pending() const { return count_ + sample_count_; }
  bool sample_poll(uint8_t channel) {
    if (!enabled_ || channel >= 48) return false;
    uint64_t ms = now().uptime_ms;
    if (!sample_seen_[channel] || ms - sample_ms_[channel] >= 5000) {
      sample_seen_[channel] = true; sample_ms_[channel] = ms; return true;
    }
    coalesced_++;
    return false;
  }
  static const char *name(EventKind k) {
    switch (k) {
#define EVENT_NAME(x) case EventKind::x: return #x
      EVENT_NAME(RESTORE); EVENT_NAME(INVITATION); EVENT_NAME(REQUEST);
      EVENT_NAME(ASSIGNMENT); EVENT_NAME(ACCEPTED); EVENT_NAME(REJECTED);
      EVENT_NAME(ACK_RETURN); EVENT_NAME(OWNERSHIP_LOST); EVENT_NAME(QUEUE_CLEARED);
      EVENT_NAME(QUEUED); EVENT_NAME(QUEUE_RETIRED); EVENT_NAME(READY);
      EVENT_NAME(WRITE_RETURN); EVENT_NAME(UART_RETURN); EVENT_NAME(QUEUE_EXPIRED); EVENT_NAME(OTHER_TOGGLE);
#undef EVENT_NAME
    }
    return "UNKNOWN";
  }
 protected:
  static void push_(BusEvent *buf, size_t cap, size_t &head, size_t &count,
                    uint32_t &lost, const BusEvent &e) {
    if (count == cap) { head = (head + 1) % cap; count--; lost++; }
    buf[(head + count) % cap] = e; count++;
  }
  bool enabled_ = false;
  ClockFn clock_;
  uint32_t seq_ = 0, lost_ = 0, sample_lost_ = 0, coalesced_ = 0;
  BusEvent critical_[CRITICAL_CAP], sample_[SAMPLE_CAP];
  size_t head_ = 0, count_ = 0, sample_head_ = 0, sample_count_ = 0;
  uint64_t sample_ms_[48] = {};
  bool sample_seen_[48] = {};
};

}  // namespace balboa_spa
}  // namespace esphome
