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
  // Bring-up only. Transmits a pattern on the UART and reports whether it reads
  // back, proving the ESP32's UART and pins independently of the RS-485 module.
  // DISCONNECT the module and jumper TX->RX before enabling: with the module
  // attached this would put bytes on a shared spa bus outside our Ready window.
  void set_uart_selftest(bool on) { uart_selftest_ = on; }
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
  // Fired when RS-485 liveness changes. Retained MQTT state stays on the broker
  // when the bus dies, so without this a stale reading is indistinguishable from
  // a fresh one — the MQTT LWT only covers the ESP32 losing its broker link.
  void add_on_bus_callback(std::function<void()> &&cb) { bus_cb_.add(std::move(cb)); }
  bool bus_connected() const { return bus_connected_; }

 protected:
  void maybe_sync_time_();

  ProtocolEngine engine_;
  CallbackManager<void()> status_cb_;
  CallbackManager<void()> config_cb_;
  CallbackManager<void()> bus_cb_;
  // The spa broadcasts Status ~1/s; treat a long gap as the bus being down.
  static constexpr uint32_t BUS_TIMEOUT_MS = 15000;
  uint32_t last_status_ms_{0};
  bool bus_connected_{false};
  // A healthy Balboa bus is ~1000 B/s; a floating rx pin yields tens of B/s of
  // EMI noise. Anything under this is not real traffic.
  static constexpr uint32_t NOISE_FLOOR_BPS = 200;
  uint32_t rx_bytes_{0};        // raw UART bytes, for bring-up diagnostics
  uint32_t last_rx_bytes_{0};
  uint32_t last_diag_ms_{0};
  bool uart_selftest_{false};
  uint32_t last_selftest_ms_{0};
  uint32_t selftest_sent_{0};
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
