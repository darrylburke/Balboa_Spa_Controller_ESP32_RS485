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

void ProtocolEngine::process_frame(const ParsedFrame &f) {
  if (decode_status(f, &status_)) {
    seen_status_ = true;
    if (on_status_update) on_status_update();
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
