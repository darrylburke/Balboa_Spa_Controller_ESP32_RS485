#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include "types.h"
#include "frame.h"

namespace esphome {
namespace balboa_spa {

class ProtocolEngine {
 public:
  using WriteFn = std::function<void(const uint8_t *, size_t)>;

  void set_write_fn(WriteFn fn) { write_fn_ = std::move(fn); }
  void set_read_only(bool ro) { read_only_ = ro; }
  void set_address(uint8_t addr) { address_ = addr; }

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

  static constexpr size_t RX_CAP = 256;
  static constexpr size_t Q_SLOTS = 16;
  static constexpr size_t Q_FRAME_CAP = 16;

  uint8_t rx_[RX_CAP];
  size_t rx_len_ = 0;

  uint8_t queue_[Q_SLOTS][Q_FRAME_CAP];
  uint8_t queue_len_[Q_SLOTS] = {0};
  size_t q_head_ = 0, q_tail_ = 0, q_count_ = 0;

  WriteFn write_fn_;
  bool read_only_ = false;
  uint8_t address_ = 0x0a;
  bool seen_status_ = false;

  SpaStatus status_;
  SpaConfig config_;
  SpaInfo info_;
  FilterCyclesData filter_;
};

}  // namespace balboa_spa
}  // namespace esphome
