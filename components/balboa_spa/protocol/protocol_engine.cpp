#include "protocol_engine.h"
#include "frame.h"
#include "messages.h"
#include <cstring>

namespace esphome {
namespace balboa_spa {

// Shapes of command encodings already implemented below; no new item semantics.
static bool valid_queued_payload(const ParsedFrame &f) {
  switch (f.type1) {
    case 0x04: return f.payload_len == 0;
    case 0x11: return f.payload_len == 2 && f.payload[1] == 0;
    case 0x20: return f.payload_len == 1;
    case 0x21: return f.payload_len == 2;
    case 0x22: return f.payload_len == 3;
    case 0x23: return f.payload_len == 8;
    case 0x27: return f.payload_len == 2;
    default: return true;  // retain compatibility for other non-flow-control frames
  }
}

void ProtocolEngine::enqueue_frame(const uint8_t *frame, size_t len) {
  if (registration_only_) {
    events_.record(EventKind::REJECTED, channel_, 0, uint32_t(RejectReason::REGISTRATION_ONLY));
    return;
  }
  if (read_only_ || !registered()) {
    events_.record(EventKind::REJECTED, channel_, 0, uint32_t(read_only_ ? RejectReason::PASSIVE : RejectReason::UNOWNED));
    return;
  }
  if (len > Q_FRAME_CAP || q_count_ >= Q_SLOTS) {
    events_.record(EventKind::REJECTED, channel_, 0, uint32_t(RejectReason::FULL));
    return;
  }  // drop if full/oversized
  // Public callers may hand us a frame encoded before ownership changed.
  // Accept one complete, CRC-valid frame only for our current channel.
  ParsedFrame parsed{};
  size_t consumed = 0;
  if (scan_frame(frame, len, &parsed, &consumed) != ScanResult::FRAME ||
      consumed != len || size_t(parsed.payload_len) + 7 != len ||
      parsed.src != channel_ || parsed.type0 != 0xbf || !valid_queued_payload(parsed) ||
      (parsed.type1 <= 0x07 && parsed.type1 != 0x04)) {
    events_.record(EventKind::REJECTED, channel_, 0, uint32_t(RejectReason::INVALID_FRAME));
    return;
  }
  queue_at_ms_[q_tail_] = events_.now().uptime_ms;
  queue_ref_[q_tail_] = events_.record(EventKind::QUEUED, channel_,
      (uint16_t(parsed.type0) << 8) | parsed.type1, len);
  std::memcpy(queue_[q_tail_], frame, len);
  queue_len_[q_tail_] = (uint8_t) len;
  q_tail_ = (q_tail_ + 1) % Q_SLOTS;
  q_count_++;
}

void ProtocolEngine::cancel_item_(uint8_t item) {
  size_t kept = 0;
  for (size_t i = 0; i < q_count_; i++) {
    size_t src = (q_head_ + i) % Q_SLOTS;
    if (queue_len_[src] == 9 && queue_[src][3] == 0xbf &&
        queue_[src][4] == 0x11 && queue_[src][5] == item) {
      events_.record(EventKind::QUEUE_RETIRED, channel_, 0xbf11,
          uint32_t(RejectReason::CONFLICT), queue_ref_[src]);
      continue;
    }
    size_t dst = (q_head_ + kept++) % Q_SLOTS;
    if (dst != src) {
      std::memcpy(queue_[dst], queue_[src], queue_len_[src]);
      queue_len_[dst] = queue_len_[src];
      queue_ref_[dst] = queue_ref_[src];
      queue_at_ms_[dst] = queue_at_ms_[src];
    }
  }
  q_count_ = kept;
  q_tail_ = (q_head_ + kept) % Q_SLOTS;
}

void ProtocolEngine::toggle_item(uint8_t item_code) {
  uint8_t f[Q_FRAME_CAP];
  size_t n = encode_toggle_item(f, item_code, channel_);
  enqueue_frame(f, n);
}

bool ProtocolEngine::send_now_(const uint8_t *frame, size_t len, TxCause cause, uint32_t ref) {
  if (read_only_ || !has_writer_()) {
    if (cause != TxCause::IDLE || trace_write_)
      events_.record(EventKind::REJECTED, channel_, 0,
          uint32_t(read_only_ ? RejectReason::PASSIVE : RejectReason::TRANSPORT), ref);
    return false;
  }
  tx_cause_ = cause; tx_ref_ = ref;
  bool ok = true;
  if (checked_write_fn_) ok = checked_write_fn_(frame, len);
  else write_fn_(frame, len);  // legacy callback: return does not report UART success
  if (cause != TxCause::IDLE || trace_write_)
    events_.record(cause == TxCause::ACK ? EventKind::ACK_RETURN : EventKind::WRITE_RETURN,
        len >= 5 ? frame[2] : 0, len >= 5 ? (uint16_t(frame[3]) << 8) | frame[4] : 0,
        (uint32_t(cause) << 16) | uint32_t(ok), ref, cause == TxCause::IDLE);
  tx_cause_ = TxCause::NONE;
  if (!ok) invalidate_ownership_(RejectReason::WRITE_FAILED);
  return ok;
}

void ProtocolEngine::invalidate_ownership_(RejectReason cause) {
  if (registered_ || registration_pending_)
    events_.record(EventKind::OWNERSHIP_LOST, channel_, 0, uint32_t(cause), registration_ref_);
  if (q_count_)
    events_.record(EventKind::QUEUE_CLEARED, channel_, 0, q_count_, uint32_t(cause));
  registered_ = false;
  registration_pending_ = false;
  q_head_ = q_tail_ = q_count_ = 0;
  statuses_since_our_window_ = 0;
  config_attempts_ = 0;
  statuses_since_config_req_ = 0;
}

// Ask the controller for a channel. Sent in response to a New Client CTS, which
// arrives about once a second, so no extra rate limiting is needed.
void ProtocolEngine::request_channel_() {
  if (registered()) return;
  if (read_only_ || !has_writer_()) {
    events_.record(EventKind::REJECTED, channel::BROADCAST, 0xbf01,
        uint32_t(read_only_ ? RejectReason::PASSIVE : RejectReason::TRANSPORT));
    return;
  }
  uint8_t f[Q_FRAME_CAP];
  size_t n = encode_id_request(f);
  registration_ref_ = events_.record(EventKind::REQUEST, channel::BROADCAST, 0xbf01,
      (uint32_t(ID_REQUEST_HASH0) << 8) | ID_REQUEST_HASH1);
  registration_pending_ = send_now_(f, n, TxCause::REQUEST, registration_ref_);
}

void ProtocolEngine::adopt_channel_(uint8_t id) {
  if (registered() || !registration_pending_ || read_only_ || !has_writer_() ||
      id < channel::MIN || id > channel::MAX) {
    events_.record(EventKind::REJECTED, id, 0xbf02, uint32_t(RejectReason::INVALID_CHANNEL), registration_ref_);
    return;
  }
  // Assignment consumes the pending exchange, but does not revoke a completed one.
  registration_pending_ = false;
  invalidate_ownership_();  // no frame encoded under an earlier channel may survive
  uint8_t f[Q_FRAME_CAP];
  size_t n = encode_id_ack(f, id);
  if (!send_now_(f, n, TxCause::ACK, registration_ref_)) return;
  channel_ = id;
  registered_ = true;
  diagnostics_.channel_adoptions++;
  events_.record(EventKind::ACCEPTED, id, 0xbf02, 0, registration_ref_);
  if (on_channel_assigned) on_channel_assigned(channel_);
}

// Ask for whatever config we are still missing, paced by Status broadcasts and
// bounded so a permanently-unanswered request cannot spam the bus forever.
void ProtocolEngine::maybe_request_config_() {
  if (read_only_ || !registered() || registration_only_) return;
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
  const uint64_t now = q_count_ ? events_.now().uptime_ms : 0;
  while (q_count_ && now - queue_at_ms_[q_head_] >= command_ttl_ms_) {
    events_.record(EventKind::QUEUE_EXPIRED, channel_, 0, command_ttl_ms_, queue_ref_[q_head_]);
    q_head_ = (q_head_ + 1) % Q_SLOTS;
    q_count_--;
  }
  if (q_count_ == 0) return false;
  if (read_only_ || !registered() || !seen_status_ || !has_writer_()) return false;
  uint32_t ref = queue_ref_[q_head_];
  bool ok = send_now_(queue_[q_head_], queue_len_[q_head_], TxCause::QUEUE, ref);
  if (!ok) return true;  // failed write revoked ownership and cleared the queue; never retry toggles
  events_.record(EventKind::QUEUE_RETIRED, channel_, 0, 1, ref);
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
      frames_decoded_++;
      process_frame(f);
      // shift out consumed bytes
      std::memmove(rx_, rx_ + consumed, rx_len_ - consumed);
      rx_len_ -= consumed;
    } else {  // NEED_MORE
      if (consumed > 0) {
        noise_bytes_ += consumed;   // discarded without yielding a frame
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
  // Keep observed state until a controller response arrives.
}

void ProtocolEngine::process_frame(const ParsedFrame &f) {
  // Count broadcasts even when already registered, and other clients' windows.
  if (is_new_client_cts(f)) {
    diagnostics_.new_client_cts++;
    if (!registered()) events_.record(EventKind::INVITATION, f.src, 0xbf00, f.payload_len);
  }
  if (is_channel_assignment(f)) {
    diagnostics_.assignments++;
    if (f.payload_len > 0) diagnostics_.last_assignment = f.payload[0];
    events_.record(EventKind::ASSIGNMENT, f.payload_len ? f.payload[0] : 0, 0xbf02,
        f.payload_len >= 3 ? (uint32_t(f.payload[1]) << 8) | f.payload[2] : 0,
        registration_pending_ ? registration_ref_ : 0);
  }
  if (is_ready(f)) {
    diagnostics_.ready++;
    diagnostics_.last_ready_channel = f.src;
    if (f.src <= channel::MAX) diagnostics_.ready_by_channel[f.src]++;
    if (is_ready_for(f, channel_)) diagnostics_.ready_ours++;
  }
  if (decode_status(f, &status_)) {
    seen_status_ = true;
    if (on_status_update) on_status_update();
    // A channel we hold but are never polled on is stale — give it up and rejoin.
    if (registered() && ++statuses_since_our_window_ >= STALE_CHANNEL_STATUSES) {
      diagnostics_.channel_stale++;
      invalidate_ownership_(RejectReason::UNOWNED);
      channel_ = channel::UNASSIGNED;
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
  // Yield unsent same-item toggles to an observed other-client action. No
  // automatic reassertion: the physical panel has priority. Missed frames cannot
  // be inferred from status alone, and are a commissioning limitation.
  if (f.src != channel_ && f.src >= channel::MIN && f.src <= channel::MAX &&
      frame_is(f, 0xbf, 0x11) && f.payload_len == 2) {
    if (events_.sample_other_frame(f.src))
      events_.record(EventKind::OTHER_TOGGLE, f.src, 0xbf11, f.payload[0], 0, true);
    // Cancellation examines every observed frame even when optional evidence
    // is coalesced. Queue retirement remains a critical event.
    cancel_item_(f.payload[0]);
  }

  // A different observed join request makes the outstanding exchange ambiguous.
  // Fail closed until the next existing NEW_CLIENT_CTS; add no backoff or delay.
  // Do not confuse an echo of our unchanged request with another client.
  if (!registered() && f.src == channel::BROADCAST &&
      frame_is(f, msg::ID_REQ0, msg::ID_REQ1) && f.payload_len == 3 &&
      (f.payload[1] != ID_REQUEST_HASH0 || f.payload[2] != ID_REQUEST_HASH1))
  {
    events_.record(EventKind::REJECTED, f.src, 0xbf01, uint32_t(RejectReason::COMPETING_REQUEST), registration_ref_);
    registration_pending_ = false;
  }

  // A new assignment of our address with a different hash contradicts exclusive
  // ownership (e.g. controller restart/reallocation). Stop answering immediately;
  // retain the address only as an observation until a fresh correlated exchange.
  if (registered() && is_channel_assignment(f) && f.payload_len == 3 &&
      f.payload[0] == channel_ &&
      (f.payload[1] != ID_REQUEST_HASH0 || f.payload[2] != ID_REQUEST_HASH1))
    invalidate_ownership_(RejectReason::CONFLICT);

  if (registered() && is_channel_assignment(f)) {
    events_.record(EventKind::REJECTED, f.payload_len ? f.payload[0] : 0, 0xbf02,
        uint32_t(RejectReason::ALREADY_REGISTERED), registration_ref_);
    return;
  }
  if (is_new_client_cts(f) && f.payload_len != 0)
    events_.record(EventKind::REJECTED, f.src, 0xbf00, uint32_t(RejectReason::MALFORMED));

  // --- channel negotiation (runs on the 0xfe broadcast channel) ---
  if (!registered()) {
    if (is_new_client_cts(f) && f.payload_len == 0) { request_channel_(); return; }
    if (is_channel_assignment(f)) {
      // Upstream responses carry channel + two request hash bytes. Shortened or
      // non-echo responses cannot be associated safely: leave them unadopted.
      if (registration_pending_ && f.payload_len == 3 &&
          f.payload[1] == ID_REQUEST_HASH0 && f.payload[2] == ID_REQUEST_HASH1)
        adopt_channel_(channel_from_assignment(f));
      else
        events_.record(EventKind::REJECTED, f.payload_len ? f.payload[0] : 0, 0xbf02,
            uint32_t(registration_pending_ ? RejectReason::CORRELATION : RejectReason::UNSOLICITED), registration_ref_);
      return;
    }
  }

  // --- transmit ONLY in the window addressed to us ---
  if (is_ready(f)) {
    if (!registered() || f.payload_len != 0 || !is_ready_for(f, channel_)) {
      if (f.src >= channel::MIN && f.src <= channel::MAX && events_.sample_poll(f.src))
        events_.record(EventKind::READY, f.src, 0xbf06, f.payload_len, 0, true);
      return;
    }   // another client's slot — stay silent
    trace_write_ = events_.sample_poll(f.src, 3);
    uint32_t poll_ref = trace_write_ ? events_.record(EventKind::READY, f.src, 0xbf06, 0, 0, true) : 0;
    statuses_since_our_window_ = 0;           // we are still being polled
    if (!pop_and_send_()) {
      // Registered clients must answer every window they are given.
      uint8_t nts[Q_FRAME_CAP];
      size_t n = encode_nothing_to_send(nts, channel_);
      send_now_(nts, n, TxCause::IDLE, poll_ref);
    }
    return;
  }
  // Unrecognized frames are ignored.
}

}  // namespace balboa_spa
}  // namespace esphome
