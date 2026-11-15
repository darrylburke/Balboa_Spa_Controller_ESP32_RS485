#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include "types.h"
#include "frame.h"
#include "events.h"
#include "messages.h"   // channel:: constants used below

namespace esphome {
namespace balboa_spa {

// Observations only: these never participate in protocol decisions.
struct BusDiagnostics {
  uint32_t new_client_cts = 0;
  uint32_t assignments = 0;
  uint8_t last_assignment = 0;  // raw payload, before existing channel clamping
  uint32_t channel_adoptions = 0;
  uint32_t channel_restores = 0;
  uint32_t channel_stale = 0;
  uint32_t ready = 0;
  uint32_t ready_ours = 0;
  uint8_t last_ready_channel = 0;
  uint32_t ready_by_channel[channel::MAX + 1] = {};
};

class ProtocolEngine {
 public:
  using CheckedWriteFn = std::function<bool(const uint8_t *, size_t)>;
  using WriteFn = std::function<void(const uint8_t *, size_t)>;

  void set_write_fn(WriteFn fn) {
    if (!fn) invalidate_ownership_(RejectReason::TRANSPORT);
    registration_pending_ = false;
    checked_write_fn_ = nullptr;
    write_fn_ = std::move(fn);
  }
  void set_checked_write_fn(CheckedWriteFn fn) {
    if (!fn) invalidate_ownership_(RejectReason::TRANSPORT);
    registration_pending_ = false;
    write_fn_ = nullptr;
    checked_write_fn_ = std::move(fn);
  }
  void set_registration_only(bool enabled) {
    if (enabled && q_count_) {
      events_.record(EventKind::QUEUE_CLEARED, channel_, 0, q_count_, uint32_t(RejectReason::REGISTRATION_ONLY));
      q_head_ = q_tail_ = q_count_ = 0;
    }
    registration_only_ = enabled;
  }
  bool registration_only() const { return registration_only_; }
  // Explicit profile opt-in: a saved address may be resumed without a fresh
  // handshake. The default remains fresh correlated registration.
  void set_resume_saved_channel(bool enabled) { resume_saved_channel_ = enabled; }
  // Local command freshness policy, not a Balboa response timing rule.
  void set_command_ttl_ms(uint32_t ttl) { command_ttl_ms_ = ttl; }
  EventRecorder &events() { return events_; }
  TxCause tx_cause() const { return tx_cause_; }
  uint32_t tx_ref() const { return tx_ref_; }
  bool trace_write() const { return trace_write_; }
  void set_read_only(bool ro) {
    if (ro) invalidate_ownership_(RejectReason::PASSIVE);
    read_only_ = ro;
  }
  // Retain the saved address for observation. Unless explicitly enabled for a
  // validated hardware profile, a fresh correlated handshake is required.
  void restore_channel(uint8_t id) {
    if (id == channel::UNASSIGNED || id > channel::MAX) return;
    invalidate_ownership_();
    channel_ = id;
    saved_channel_candidate_ = id;
    diagnostics_.channel_restores++;
    events_.record(EventKind::RESTORE, id);
    if (resume_saved_channel_ && !read_only_ && !registration_only_) {
      registered_ = true;
      events_.record(EventKind::RESUME, id);
    }
  }
  const BusDiagnostics &diagnostics() const { return diagnostics_; }

  uint8_t channel() const { return channel_; }
  bool registered() const { return registered_; }

  // Fired once when the controller assigns a channel via the handshake, so the
  // caller can persist it. Not fired on restore_channel().
  std::function<void(uint8_t)> on_channel_assigned;

  // Fired when a held channel goes stale and is released (spa forgot us).
  std::function<void()> on_channel_stale;

  // Fired if a fresh assignment differs from a saved channel. This signals a
  // possible leaked slot; the protocol cannot confirm whether the old slot is
  // still allocated.
  std::function<void(uint8_t, uint8_t)> on_possible_channel_leak;

  uint8_t config_attempts() const { return config_attempts_; }
  bool gave_up_on_config() const { return config_attempts_ >= CONFIG_MAX_ATTEMPTS; }

  // Bring-up diagnostics. Distinguishing "no bytes at all" from "bytes that never
  // decode" is the difference between a wiring fault and swapped A/B — without
  // these counters both look identical from the outside.
  uint32_t frames_decoded() const { return frames_decoded_; }
  uint32_t noise_bytes() const { return noise_bytes_; }

  void feed(const uint8_t *data, size_t len);

  const SpaStatus &status() const { return status_; }
  const SpaConfig &config() const { return config_; }
  const SpaInfo &info() const { return info_; }
  const FilterCyclesData &filter_cycles() const { return filter_; }
  bool have_full_config() const {
    return status_.valid && info_.valid && config_.valid && filter_.valid;
  }

  void enqueue_frame(const uint8_t *frame, size_t len);
  void toggle_item(uint8_t item_code);

  void request_missing_config();
  void set_target_temperature_raw(uint8_t raw);
  void set_light(uint8_t index, bool on);
  void set_aux(uint8_t index, bool on);
  void set_mister(bool on);
  void set_hold(bool on);
  void set_pump(uint8_t index, uint8_t desired_speed);
  void set_blower(uint8_t desired_level);
  void set_heating_mode(HeatingMode desired);
  void set_temperature_range(TempRange desired);
  void set_temperature_scale(TempScale scale);
  void set_time(uint8_t hour, uint8_t minute, bool h24);
  void update_filter_cycles(const FilterCyclesData &fc);

  std::function<void()> on_status_update;
  std::function<void()> on_config_update;

 protected:
  void process_frame(const ParsedFrame &f);
  bool pop_and_send_();
  bool send_now_(const uint8_t *frame, size_t len, TxCause cause = TxCause::IDLE, uint32_t ref = 0);   // bypasses the queue (handshake frames)
  void invalidate_ownership_(RejectReason cause = RejectReason::UNOWNED);
  void cancel_item_(uint8_t item);
  bool has_writer_() const { return bool(write_fn_) || bool(checked_write_fn_); }
  void request_channel_();
  void adopt_channel_(uint8_t id);
  void maybe_request_config_();

  static constexpr size_t RX_CAP = 256;
  static constexpr size_t Q_SLOTS = 16;
  static constexpr size_t Q_FRAME_CAP = 16;
  // Config requests are retried, because a single dropped response otherwise
  // stalls have_full_config() forever (observed once on real hardware). Retries
  // are paced by Status broadcasts (~1/s) and bounded so we never spam the bus.
  static constexpr uint16_t CONFIG_RETRY_STATUSES = 10;
  static constexpr uint8_t CONFIG_MAX_ATTEMPTS = 20;
  // If the controller stops offering us windows for this many Status broadcasts,
  // our channel is stale (e.g. the spa was power-cycled and forgot us). Drop back
  // to unassigned so the join handshake runs again instead of answering nobody.
  static constexpr uint16_t STALE_CHANNEL_STATUSES = 30;

  uint8_t rx_[RX_CAP];
  size_t rx_len_ = 0;

  uint8_t queue_[Q_SLOTS][Q_FRAME_CAP];
  uint8_t queue_len_[Q_SLOTS] = {0};
  size_t q_head_ = 0, q_tail_ = 0, q_count_ = 0;

  EventRecorder events_;
  TxCause tx_cause_ = TxCause::NONE;
  uint32_t tx_ref_ = 0, registration_ref_ = 0;
  bool trace_write_ = true;
  uint32_t queue_ref_[Q_SLOTS] = {};
  uint64_t queue_at_ms_[Q_SLOTS] = {};
  uint32_t command_ttl_ms_ = 5000;
  CheckedWriteFn checked_write_fn_;
  WriteFn write_fn_;
  bool read_only_ = false;
  bool registration_only_ = false;
  bool resume_saved_channel_ = false;
  bool registered_ = false;
  bool registration_pending_ = false;
  // A saved address is diagnostic-only unless the explicit resume policy is on.
  uint8_t channel_ = channel::UNASSIGNED;
  uint8_t saved_channel_candidate_ = channel::UNASSIGNED;
  bool seen_status_ = false;
  uint8_t config_attempts_ = 0;
  uint16_t statuses_since_config_req_ = 0;
  uint16_t statuses_since_our_window_ = 0;
  uint32_t frames_decoded_ = 0;
  uint32_t noise_bytes_ = 0;

  BusDiagnostics diagnostics_;
  SpaStatus status_;
  SpaConfig config_;
  SpaInfo info_;
  FilterCyclesData filter_;
};

}  // namespace balboa_spa
}  // namespace esphome
