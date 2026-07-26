#include "protocol_engine.h"
#include "frame.h"
#include "messages.h"
#include <cstring>

namespace esphome {
namespace balboa_spa {

void ProtocolEngine::enqueue_frame(const uint8_t *frame, size_t len) {
  if (len > Q_FRAME_CAP || q_count_ >= Q_SLOTS) return;  // drop if full/oversized
  std::memcpy(queue_[q_tail_], frame, len);
  queue_len_[q_tail_] = (uint8_t) len;
  q_tail_ = (q_tail_ + 1) % Q_SLOTS;
  q_count_++;
}

void ProtocolEngine::toggle_item(uint8_t item_code) {
  uint8_t f[Q_FRAME_CAP];
  size_t n = encode_toggle_item(f, item_code, channel_);
  enqueue_frame(f, n);
}

void ProtocolEngine::send_now_(const uint8_t *frame, size_t len) {
  if (read_only_ || !write_fn_) return;
  write_fn_(frame, len);
}

// Ask the controller for a channel. Sent in response to a New Client CTS, which
// arrives about once a second, so no extra rate limiting is needed.
void ProtocolEngine::request_channel_() {
  if (registered()) return;
  uint8_t f[Q_FRAME_CAP];
  size_t n = encode_id_request(f);
  send_now_(f, n);
}

void ProtocolEngine::adopt_channel_(uint8_t id) {
  if (registered() || id == channel::UNASSIGNED) return;
  channel_ = id;
  uint8_t f[Q_FRAME_CAP];
  size_t n = encode_id_ack(f, channel_);
  send_now_(f, n);
  if (on_channel_assigned) on_channel_assigned(channel_);   // persist it
}

// Ask for whatever config we are still missing, paced by Status broadcasts and
// bounded so a permanently-unanswered request cannot spam the bus forever.
void ProtocolEngine::maybe_request_config_() {
  if (have_full_config()) { config_attempts_ = 0; statuses_since_config_req_ = 0; return; }
  if (q_count_ != 0) return;                       // wait for the queue to drain
  statuses_since_config_req_++;
  if (config_attempts_ != 0 && statuses_since_config_req_ < CONFIG_RETRY_STATUSES) return;
  if (config_attempts_ >= CONFIG_MAX_ATTEMPTS) return;   // gave up
  request_missing_config();
  statuses_since_config_req_ = 0;
  config_attempts_++;
}

bool ProtocolEngine::pop_and_send_() {
  if (q_count_ == 0) return false;
  if (read_only_ || !seen_status_ || !write_fn_) return false;
  write_fn_(queue_[q_head_], queue_len_[q_head_]);
  q_head_ = (q_head_ + 1) % Q_SLOTS;
  q_count_--;
  return true;
}

void ProtocolEngine::feed(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (rx_len_ < RX_CAP) rx_[rx_len_++] = data[i];
    // else: overflow — drop oldest by shifting half (rare; keeps us alive)
    else {
      std::memmove(rx_, rx_ + RX_CAP / 2, RX_CAP / 2);
      rx_len_ = RX_CAP / 2;
      rx_[rx_len_++] = data[i];
    }
  }
  // Parse as many frames as available.
  while (true) {
    ParsedFrame f{};
    size_t consumed = 0;
    ScanResult r = scan_frame(rx_, rx_len_, &f, &consumed);
    if (r == ScanResult::FRAME) {
      process_frame(f);
      // shift out consumed bytes
      std::memmove(rx_, rx_ + consumed, rx_len_ - consumed);
      rx_len_ -= consumed;
    } else {  // NEED_MORE
      if (consumed > 0) {
        std::memmove(rx_, rx_ + consumed, rx_len_ - consumed);
        rx_len_ -= consumed;
      }
      break;
    }
  }
}

void ProtocolEngine::request_missing_config() {
  uint8_t f[Q_FRAME_CAP];
  if (!info_.valid)   { size_t n = encode_control_config_request(f, 1, channel_); enqueue_frame(f, n); }
  if (!config_.valid) { size_t n = encode_control_config_request(f, 2, channel_); enqueue_frame(f, n); }
  if (!filter_.valid) { size_t n = encode_control_config_request(f, 3, channel_); enqueue_frame(f, n); }
}

void ProtocolEngine::set_target_temperature_raw(uint8_t raw) {
  uint8_t f[Q_FRAME_CAP];
  size_t n = encode_set_target_temp(f, raw, channel_);
  enqueue_frame(f, n);
}

void ProtocolEngine::set_light(uint8_t index, bool on) {
  if (index > 1) return;
  if (status_.valid && status_.lights[index] == on) return;
  toggle_item(item::LIGHT1 + index);
}

void ProtocolEngine::set_aux(uint8_t index, bool on) {
  if (index > 1) return;
  if (status_.valid && status_.aux[index] == on) return;
  toggle_item(item::AUX1 + index);
}

void ProtocolEngine::set_mister(bool on) {
  if (status_.valid && status_.mister == on) return;
  toggle_item(item::MISTER);
}

void ProtocolEngine::set_hold(bool on) {
  if (status_.valid && status_.hold == on) return;
  toggle_item(item::HOLD);
}

void ProtocolEngine::set_pump(uint8_t index, uint8_t desired_speed) {
  if (index > 5 || !status_.valid || !config_.valid) return;
  uint8_t max_speed = config_.pumps[index];
  if (max_speed == 0) return;
  if (desired_speed > max_speed) desired_speed = max_speed;
  uint8_t current = status_.pumps[index];
  if (current > max_speed) current = max_speed;
  uint8_t times = (uint8_t)(((int) desired_speed - (int) current + (max_speed + 1)) % (max_speed + 1));
  for (uint8_t i = 0; i < times; i++) toggle_item(item::PUMP1 + index);
}

void ProtocolEngine::set_blower(uint8_t desired_level) {
  if (!status_.valid || !config_.valid) return;
  uint8_t max_level = config_.blower;
  if (max_level == 0) return;
  if (desired_level > max_level) desired_level = max_level;
  uint8_t current = status_.blower;
  if (current > max_level) current = max_level;
  uint8_t times = (uint8_t)(((int) desired_level - (int) current + (max_level + 1)) % (max_level + 1));
  for (uint8_t i = 0; i < times; i++) toggle_item(item::BLOWER);
}

void ProtocolEngine::set_heating_mode(HeatingMode desired) {
  if (!status_.valid) return;
  HeatingMode cur = status_.heating_mode;
  uint8_t times = 0;
  if ((cur == HeatingMode::READY && desired == HeatingMode::REST) ||
      (cur == HeatingMode::REST && desired == HeatingMode::READY) ||
      (cur == HeatingMode::READY_IN_REST && desired == HeatingMode::REST)) {
    times = 1;
  } else if (cur == HeatingMode::READY_IN_REST && desired == HeatingMode::READY) {
    times = 2;
  }
  for (uint8_t i = 0; i < times; i++) toggle_item(item::HEATING_MODE);
}

void ProtocolEngine::set_temperature_range(TempRange desired) {
  if (status_.valid && status_.temp_range == desired) return;
  toggle_item(item::TEMPERATURE_RANGE);
}

void ProtocolEngine::set_temperature_scale(TempScale scale) {
  uint8_t f[Q_FRAME_CAP];
  size_t n = encode_set_temp_scale(f, scale, channel_);
  enqueue_frame(f, n);
}

void ProtocolEngine::set_time(uint8_t hour, uint8_t minute, bool h24) {
  uint8_t f[Q_FRAME_CAP];
  size_t n = encode_set_time(f, hour, minute, h24, channel_);
  enqueue_frame(f, n);
}

void ProtocolEngine::update_filter_cycles(const FilterCyclesData &fc) {
  uint8_t f[Q_FRAME_CAP + 8];
  size_t n = encode_filter_cycles(f, fc, channel_);
  enqueue_frame(f, n);
  filter_ = fc;
}

void ProtocolEngine::process_frame(const ParsedFrame &f) {
  if (decode_status(f, &status_)) {
    seen_status_ = true;
    if (on_status_update) on_status_update();
    // A channel we hold but are never polled on is stale — give it up and rejoin.
    if (registered() && ++statuses_since_our_window_ >= STALE_CHANNEL_STATUSES) {
      channel_ = channel::UNASSIGNED;
      statuses_since_our_window_ = 0;
      if (on_channel_stale) on_channel_stale();
    }
    maybe_request_config_();
    return;
  }
  // Any config response is progress: reset the retry budget so a later stall
  // still gets its own full allowance of attempts.
  if (decode_control_config(f, &info_)) {
    config_attempts_ = 0; statuses_since_config_req_ = 0;
    if (on_config_update) on_config_update();
    return;
  }
  if (decode_control_config2(f, &config_)) {
    config_attempts_ = 0; statuses_since_config_req_ = 0;
    if (on_config_update) on_config_update();
    return;
  }
  if (decode_filter_cycles(f, &filter_)) {
    config_attempts_ = 0; statuses_since_config_req_ = 0;
    if (on_config_update) on_config_update();
    return;
  }
  // --- channel negotiation (runs on the 0xfe broadcast channel) ---
  if (!registered()) {
    if (is_new_client_cts(f)) { request_channel_(); return; }
    if (is_channel_assignment(f)) { adopt_channel_(channel_from_assignment(f)); return; }
  }

  // --- transmit ONLY in the window addressed to us ---
  if (is_ready(f)) {
    if (!is_ready_for(f, channel_)) return;   // another client's slot — stay silent
    statuses_since_our_window_ = 0;           // we are still being polled
    if (!pop_and_send_()) {
      // Registered clients must answer every window they are given.
      uint8_t nts[Q_FRAME_CAP];
      size_t n = encode_nothing_to_send(nts, channel_);
      send_now_(nts, n);
    }
    return;
  }
  // Unrecognized frames are ignored.
}

}  // namespace balboa_spa
}  // namespace esphome
