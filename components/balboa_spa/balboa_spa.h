#pragma once
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "esphome/components/uart/uart.h"
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif
#include "protocol/protocol_engine.h"

namespace esphome {
namespace balboa_spa {

class BalboaSpa : public Component, public uart::UARTDevice {
 public:
  float get_setup_priority() const override { return setup_priority::DATA; }
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_read_only(bool ro) { read_only_ = ro; }
#ifdef USE_TIME
  void set_time(time::RealTimeClock *rtc) { time_ = rtc; }
#endif

  ProtocolEngine &engine() { return engine_; }
  const SpaStatus &status() const { return engine_.status(); }
  const SpaConfig &config() const { return engine_.config(); }
  const SpaInfo &info() const { return engine_.info(); }
  const FilterCyclesData &filter_cycles() const { return engine_.filter_cycles(); }

  void add_on_status_callback(std::function<void()> &&cb) { status_cb_.add(std::move(cb)); }
  void add_on_config_callback(std::function<void()> &&cb) { config_cb_.add(std::move(cb)); }

 protected:
  void maybe_sync_time_();

  ProtocolEngine engine_;
  CallbackManager<void()> status_cb_;
  CallbackManager<void()> config_cb_;
#ifdef USE_TIME
  time::RealTimeClock *time_{nullptr};
#endif
  bool read_only_{true};
  bool discovery_logged_{false};
  uint32_t last_time_sync_{0};
  uint8_t rx_chunk_[128];
  // Assigned bus channel, persisted across reboots. Without this every restart
  // negotiates a NEW channel and the controller polls the old one forever.
  ESPPreferenceObject channel_pref_;
};

}  // namespace balboa_spa
}  // namespace esphome
