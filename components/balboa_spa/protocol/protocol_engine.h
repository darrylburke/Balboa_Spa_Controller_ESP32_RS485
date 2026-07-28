#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include "types.h"
#include "frame.h"
#include "messages.h"   // channel:: constants used below

namespace esphome {
namespace balboa_spa {

class ProtocolEngine {
 public:
  using WriteFn = std::function<void(const uint8_t *, size_t)>;

  void set_write_fn(WriteFn fn) { write_fn_ = std::move(fn); }
  void set_read_only(bool ro) { read_only_ = ro; }
  // Resume a channel the controller assigned to us in an earlier session (e.g.
  // reloaded from NVS after a reboot). The controller keeps polling a channel
  // forever once assigned, so we can answer it directly with no handshake —
  // verified on hardware. This is what stops every reboot leaking a new channel.
  void restore_channel(uint8_t id) {
    if (id == channel::UNASSIGNED || id > channel::MAX) return;
    channel_ = id;
  }

  uint8_t channel() const { return channel_; }
  bool registered() const { return channel_ != channel::UNASSIGNED; }

  // Fired once when the controller assigns a channel via the handshake, so the
  // caller can persist it. Not fired on restore_channel().
  std::function<void(uint8_t)> on_channel_assigned;

  // Fired when a held channel goes stale and is released (spa forgot us).
  std::function<void()> on_channel_stale;

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
  void send_now_(const uint8_t *frame, size_t len);   // bypasses the queue (handshake frames)
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

  WriteFn write_fn_;
  bool read_only_ = false;
  // Starts unassigned: we stay silent until the controller gives us a channel.
  uint8_t channel_ = channel::UNASSIGNED;
  bool seen_status_ = false;
  uint8_t config_attempts_ = 0;
  uint16_t statuses_since_config_req_ = 0;
  uint16_t statuses_since_our_window_ = 0;
  uint32_t frames_decoded_ = 0;
  uint32_t noise_bytes_ = 0;

  SpaStatus status_;
  SpaConfig config_;
  SpaInfo info_;
  FilterCyclesData filter_;
};

}  // namespace balboa_spa
}  // namespace esphome
