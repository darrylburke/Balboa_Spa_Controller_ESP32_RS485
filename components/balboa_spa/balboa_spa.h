#pragma once
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "esphome/components/uart/uart.h"
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif
#include "protocol/protocol_engine.h"
#include "protocol/status_publication.h"
#include "protocol/transport_write.h"
#include "protocol/boot_trace.h"
#include <string>

namespace esphome {
namespace balboa_spa {

class BalboaSpa : public Component, public uart::UARTDevice {
 public:
  float get_setup_priority() const override { return setup_priority::DATA; }
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_read_only(bool ro) { read_only_ = ro; engine_.set_read_only(ro); }
  // Bring-up only. Transmits a pattern on the UART and reports whether it reads
  // back, proving the ESP32's UART and pins independently of the RS-485 module.
  // DISCONNECT the module and jumper TX->RX before enabling: with the module
  // attached this would put bytes on a shared spa bus outside our Ready window.
  void set_uart_selftest(bool on) { uart_selftest_ = on; }
  // DE+/RE of a MAX485-style module. Held LOW (receive) except while writing.
  void set_uart_health(Component *component) { uart_component_ = component; }
  void set_direction_pin(GPIOPin *pin) { direction_pin_ = pin; }
#ifdef USE_TIME
  void set_time(time::RealTimeClock *rtc) { time_ = rtc; }
#endif

  void set_mqtt_command_state(bool enabled) { mqtt_command_state_ = enabled; }
  void set_registration_only(bool enabled) { engine_.set_registration_only(enabled); }
  void set_command_ttl_ms(uint32_t ttl) { engine_.set_command_ttl_ms(ttl); }
  void set_diagnostic_events(bool enabled) { engine_.events().set_enabled(enabled); }
  uint32_t diagnostic_boot_id() const { return diagnostic_boot_id_; }
  const BootTrace &diagnostic_boot_trace() const;
  bool transport_faulted() const { return transport_write_.faulted(); }
  bool read_only() const { return read_only_; }
  bool uart_selftest() const { return uart_selftest_; }
  uint32_t tx_frames() const { return tx_frames_; }
  uint32_t tx_bytes() const { return tx_bytes_; }
  uint8_t last_tx_channel() const { return last_tx_channel_; }
  uint16_t last_tx_type() const { return last_tx_type_; }

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
  void publish_command_response_(const char *request, const char *command,
                                 const char *value, uint8_t index);
  bool publish_config_snapshot_();
  void handle_mqtt_command_(const std::string &payload);
  void publish_status_response_(uint8_t selector);
  void publish_debug_response_(uint8_t selector);
  bool bus_write_(const uint8_t *d, size_t n, bool selftest = false);
  TransportWrite transport_write_;

  uint32_t diagnostic_boot_id_{0};
  uint32_t tx_frames_{0};
  uint32_t tx_bytes_{0};
  uint8_t last_tx_channel_{0};
  uint16_t last_tx_type_{0};
  ProtocolEngine engine_;
  CallbackManager<void()> status_cb_;
  CallbackManager<void()> config_cb_;
  CallbackManager<void()> bus_cb_;
  // The spa broadcasts Status ~1/s; treat a long gap as the bus being down.
  static constexpr uint32_t BUS_TIMEOUT_MS = 15000;
  uint32_t last_status_ms_{0};
  StatusPublication status_publication_;
  std::string status_topic_;
  bool mqtt_command_state_{false};
  bool config_publish_pending_{false};
  bool bus_connected_{false};
  // A healthy Balboa bus is ~1000 B/s; a floating rx pin yields tens of B/s of
  // EMI noise. Anything under this is not real traffic.
  static constexpr uint32_t NOISE_FLOOR_BPS = 200;
  uint32_t rx_bytes_{0};        // raw UART bytes, for bring-up diagnostics
  uint32_t last_rx_bytes_{0};
  uint32_t last_diag_ms_{0};
  bool uart_selftest_{false};
  Component *uart_component_{nullptr};
  GPIOPin *direction_pin_{nullptr};
  uint32_t last_selftest_ms_{0};
  uint32_t selftest_sent_{0};
#ifdef USE_TIME
  time::RealTimeClock *time_{nullptr};
#endif
  bool read_only_{true};
  bool discovery_logged_{false};
  uint32_t last_time_sync_{0};
  uint8_t rx_chunk_[128];
  // Last assigned bus channel, persisted for observation only.
  // A saved value does not authorize transmission after reboot.
  ESPPreferenceObject channel_pref_;
};

}  // namespace balboa_spa
}  // namespace esphome
