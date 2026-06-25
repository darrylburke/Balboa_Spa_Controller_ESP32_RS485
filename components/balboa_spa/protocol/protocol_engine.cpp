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
  size_t n = encode_toggle_item(f, item_code);
  enqueue_frame(f, n);
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
  if (!info_.valid)   { size_t n = encode_control_config_request(f, 1); enqueue_frame(f, n); }
  if (!config_.valid) { size_t n = encode_control_config_request(f, 2); enqueue_frame(f, n); }
  if (!filter_.valid) { size_t n = encode_control_config_request(f, 3); enqueue_frame(f, n); }
}

void ProtocolEngine::set_target_temperature_raw(uint8_t raw) {
  uint8_t f[Q_FRAME_CAP];
  size_t n = encode_set_target_temp(f, raw);
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
  size_t n = encode_set_temp_scale(f, scale);
  enqueue_frame(f, n);
}

void ProtocolEngine::set_time(uint8_t hour, uint8_t minute, bool h24) {
  uint8_t f[Q_FRAME_CAP];
  size_t n = encode_set_time(f, hour, minute, h24);
  enqueue_frame(f, n);
}

void ProtocolEngine::update_filter_cycles(const FilterCyclesData &fc) {
  uint8_t f[Q_FRAME_CAP + 8];
  size_t n = encode_filter_cycles(f, fc);
  enqueue_frame(f, n);
  filter_ = fc;
}

void ProtocolEngine::process_frame(const ParsedFrame &f) {
  if (decode_status(f, &status_)) {
    seen_status_ = true;
    if (on_status_update) on_status_update();
    if (!have_full_config() && q_count_ == 0) request_missing_config();
    return;
  }
  if (decode_control_config(f, &info_)) {
    if (on_config_update) on_config_update();
    return;
  }
  if (decode_control_config2(f, &config_)) {
    if (on_config_update) on_config_update();
    return;
  }
  if (decode_filter_cycles(f, &filter_)) {
    if (on_config_update) on_config_update();
    return;
  }
  if (is_ready(f)) {
    pop_and_send_();
    return;
  }
  // NewClientCTS and unrecognized frames are ignored.
}

}  // namespace balboa_spa
}  // namespace esphome
