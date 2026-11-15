#include "balboa_spa.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <sstream>
#ifdef USE_ESP32
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_wifi.h>
#endif
#include "protocol/temperature.h"
#ifdef USE_MQTT
#include "esphome/components/mqtt/mqtt_client.h"
#endif

namespace esphome {
namespace balboa_spa {

static const char *const TAG = "balboa_spa";
#ifdef USE_ESP32
RTC_NOINIT_ATTR static BootTrace boot_trace;
#else
static BootTrace boot_trace;
#endif

const BootTrace &BalboaSpa::diagnostic_boot_trace() const { return boot_trace; }

void BalboaSpa::setup() {
#ifdef USE_ESP32
  const auto reason = esp_reset_reason();
  boot_trace.begin(uint32_t(reason), reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT ||
      reason == ESP_RST_WDT || reason == ESP_RST_PANIC);
#else
  boot_trace.begin(0, false);
#endif
  diagnostic_boot_id_ = random_uint32();
  engine_.events().set_clock([]() {
    std::time_t utc = std::time(nullptr);
    // System time is unset before synchronization. Do not enable a clock source
    // or spa time-sync just to obtain diagnostics; absent UTC stays null.
    return EventTime{millis_64(), utc >= 1577836800 && uint64_t(utc) <= UINT32_MAX ? uint32_t(utc) : 0};
  });
  if (direction_pin_ != nullptr) {
    // Receive by default. Left floating, a MAX485's DE/RE can enable its driver
    // and hold the shared bus, and the receiver never passes anything to RX.
    direction_pin_->setup();
    direction_pin_->digital_write(false);
  }
  engine_.set_read_only(read_only_);
  engine_.set_checked_write_fn([this](const uint8_t *d, size_t n) { return this->bus_write_(d, n); });

  // Load the previous address. By default it is diagnostic only and active
  // operation must register again; a validated hardware profile may opt in to
  // resuming it through the explicit YAML setting.
  this->channel_pref_ = global_preferences->make_preference<uint8_t>(fnv1_hash("balboa_spa_channel"));
  uint8_t saved = 0;
  if (this->channel_pref_.load(&saved)) {
    engine_.restore_channel(saved);
    if (engine_.channel() == saved && saved != channel::UNASSIGNED) {
      if (engine_.registered())
        ESP_LOGW(TAG, "Resuming configured saved bus channel 0x%02X without revalidation", saved);
      else
        ESP_LOGI(TAG, "Saved bus channel 0x%02X (unverified; fresh registration required)", saved);
    } else {
      ESP_LOGW(TAG, "Saved bus channel 0x%02X is invalid; will negotiate a new one", saved);
    }
  }
  engine_.on_channel_assigned = [this](uint8_t ch) {
    ESP_LOGI(TAG, "Controller assigned bus channel 0x%02X (persisting)", ch);
    this->channel_pref_.save(&ch);
  };
  engine_.on_channel_stale = [this]() {
    // The spa no longer polls our saved channel (typically after a spa power
    // cycle). Forget it so we negotiate a fresh one instead of talking to nobody.
    ESP_LOGW(TAG, "Bus channel went stale (no windows offered); rejoining");
    uint8_t none = 0;
    this->channel_pref_.save(&none);
  };
  engine_.on_possible_channel_leak = [](uint8_t saved, uint8_t assigned) {
    ESP_LOGW(TAG, "Possible channel leak: saved slot 0x%02X replaced by 0x%02X; the controller may still poll the saved slot",
        saved, assigned);
  };
  engine_.on_status_update = [this]() {
    boot_trace.mark(ExecutionStage::STATUS_CALLBACK);
    this->last_status_ms_ = millis();
    if (!this->bus_connected_) {
      this->bus_connected_ = true;
      ESP_LOGI(TAG, "RS-485 bus connected (Status received)");
      this->bus_cb_.call();
    }
    // The spa repeats Status several times a second. Sensors, climate, fan and
    // select publish on every call, so fanning out each one flooded MQTT (12k
    // dropped messages/10s) and kept knocking the broker link over. Notify
    // entities on change, with a bounded periodic refresh.
    if (this->status_publication_.observe(this->engine_.status(), millis_64())) {
      boot_trace.mark(ExecutionStage::ENTITY_CALLBACKS);
      // HA consumes plain values on its per-entity topics. The JSON STATUS
      // envelope is only sent in response to a STATUS command.
      this->status_cb_.call();
    }
    boot_trace.mark(ExecutionStage::TIME_SYNC);
    this->maybe_sync_time_();
    boot_trace.mark(ExecutionStage::PROTOCOL_FEED);
  };
  engine_.on_config_update = [this]() {
    this->config_cb_.call();
    if (this->mqtt_command_state_) this->config_publish_pending_ = true;
    if (!this->discovery_logged_ && this->engine_.config().valid && this->engine_.info().valid) {
      const SpaConfig &c = this->engine_.config();
      const SpaInfo &i = this->engine_.info();
      ESP_LOGI(TAG, "Detected spa: model='%s' version='%s'", i.model, i.version);
      for (int p = 0; p < 6; p++)
        if (c.pumps[p]) ESP_LOGI(TAG, "  pump%d: %d-speed", p + 1, c.pumps[p]);
      for (int l = 0; l < 2; l++)
        if (c.lights[l]) ESP_LOGI(TAG, "  light%d present", l + 1);
      if (c.circulation_pump) ESP_LOGI(TAG, "  circulation pump present");
      if (c.blower) ESP_LOGI(TAG, "  blower: %d-level", c.blower);
      if (c.mister) ESP_LOGI(TAG, "  mister present");
      for (int a = 0; a < 2; a++)
        if (c.aux[a]) ESP_LOGI(TAG, "  aux%d present", a + 1);
      this->discovery_logged_ = true;
    }
  };
#ifdef USE_MQTT
  if (this->mqtt_command_state_) {
    auto *client = mqtt::global_mqtt_client;
    if (client != nullptr) {
      std::string command_topic = client->get_topic_prefix();
      if (!command_topic.empty() && command_topic.back() != '/') command_topic += '/';
      command_topic += "command";
      client->subscribe(command_topic,
          [this](const std::string &, const std::string &payload) { this->handle_mqtt_command_(payload); }, 1);
    }
  }
#endif
  boot_trace.mark(ExecutionStage::OUTSIDE);
}

void BalboaSpa::loop() {
  boot_trace.mark(ExecutionStage::UART_AVAILABLE);
  int avail = this->available();
  while (avail > 0) {
    int n = avail > (int) sizeof(rx_chunk_) ? (int) sizeof(rx_chunk_) : avail;
    boot_trace.mark(ExecutionStage::UART_READ);
    this->read_array(rx_chunk_, n);
    rx_bytes_ += (uint32_t) n;
    boot_trace.mark(ExecutionStage::PROTOCOL_FEED);
    engine_.feed(rx_chunk_, n);
    avail -= n;
  }

  boot_trace.mark(ExecutionStage::OUTSIDE);
  // Bring-up diagnostic: report what the UART is actually seeing. The three
  // outcomes point at different faults, so print them until the bus is healthy:
  //   bytes=0                  -> nothing reaching RX: wiring, TX/RX not crossed,
  //                               wrong pins, or no signal on A/B
  //   bytes>0, frames=0        -> signal present but never decodes: A/B swapped
  //                               (or wrong baud)
  //   frames>0                 -> bus is healthy
  uint32_t now = millis();

  // UART loopback self-test. Proves the ESP32's own UART + GPIO pins work,
  // independently of the RS-485 module. Jumper TX->RX with the module removed:
  // bytes coming back means the ESP32 side is healthy and the fault is the
  // module or its wiring; still zero means the problem is here, not out there.
  if (uart_selftest_ && (now - last_selftest_ms_) > 3000) {
    last_selftest_ms_ = now;
    static const uint8_t pattern[8] = {0x7e, 0x55, 0xaa, 0x0f, 0xf0, 0x00, 0xff, 0x7e};

    // Drain anything already buffered so noise from a previous interval cannot
    // be mistaken for our echo.
    while (this->available()) { uint8_t d; this->read_byte(&d); }

    this->bus_write_(pattern, sizeof(pattern), true);
    delay(20);   // 8 bytes @115200 is ~0.7ms; allow ample margin

    // Compare CONTENT, not just a count. Checking "did any bytes arrive" is
    // useless here: a floating rx pin produces tens of bytes/sec of EMI, which
    // makes a count-based test report success even with no jumper fitted.
    uint8_t got[16];
    size_t n = 0;
    while (this->available() && n < sizeof(got)) { this->read_byte(&got[n]); n++; }
    bool match = (n >= sizeof(pattern)) &&
                 std::memcmp(got, pattern, sizeof(pattern)) == 0;
    selftest_sent_ += sizeof(pattern);
    rx_bytes_ += (uint32_t) n;   // keep the byte counter honest

    if (match) {
      ESP_LOGI(TAG, "UART selftest: PASS — pattern echoed exactly (ESP32 UART + pins are good)");
    } else if (n == 0) {
      ESP_LOGW(TAG, "UART selftest: FAIL — nothing came back (jumper missing, wrong pins, or dead GPIO)");
    } else {
      ESP_LOGW(TAG, "UART selftest: FAIL — got %u bytes but they do not match the pattern "
                    "(first=0x%02X) — likely noise, not a real echo", (unsigned) n, got[0]);
    }
    return;   // don't also print the bus diag this pass
  }

  if (!bus_connected_ && (now - last_diag_ms_) > 10000) {
    uint32_t elapsed = now - last_diag_ms_;
    uint32_t delta = rx_bytes_ - last_rx_bytes_;
    uint32_t rate = elapsed ? (delta * 1000UL) / elapsed : 0;   // bytes/sec
    last_diag_ms_ = now;
    last_rx_bytes_ = rx_bytes_;

    // Rate is what discriminates, not the cumulative count. A healthy Balboa bus
    // runs ~1000 B/s. A FLOATING rx pin still produces tens of bytes/sec of EMI
    // noise, which looks like "signal" if you only watch the total go up.
    const char *verdict;
    if (rate == 0)
      verdict = "NO BYTES (rx pin held / nothing arriving)";
    else if (rate < NOISE_FLOOR_BPS)
      verdict = "NOISE ONLY — rx pin is probably FLOATING (module not wired to RX?)";
    else if (engine_.frames_decoded() == 0)
      verdict = "real signal but nothing decodes (swap A+/B-, or wrong baud)";
    else
      verdict = "frames decoding, awaiting Status";

    ESP_LOGI(TAG, "RS-485 diag: %u B/s (total=%u) frames=%u — %s",
             (unsigned) rate, (unsigned) rx_bytes_,
             (unsigned) engine_.frames_decoded(), verdict);
  }

  // Detect the bus going quiet so the non-retained per-entity bus-connected
  // state changes immediately and the user can distinguish stale spa data.
  if (this->bus_connected_ && (millis() - this->last_status_ms_) > BUS_TIMEOUT_MS) {
    this->bus_connected_ = false;
    ESP_LOGW(TAG, "RS-485 bus lost (no Status for %ums); published values are now stale",
             (unsigned) BUS_TIMEOUT_MS);
    this->bus_cb_.call();
  }
  if (this->mqtt_command_state_ && this->config_publish_pending_ && this->publish_config_snapshot_())
    this->config_publish_pending_ = false;
}

bool BalboaSpa::bus_write_(const uint8_t *d, size_t n, bool selftest) {
  ExecutionScope execution(boot_trace, ExecutionStage::TX_CHECK);
  boot_trace.frame_type = n >= 5 ? (uint32_t(d[3]) << 8) | d[4] : 0;
  boot_trace.length = n;
  // Final transport guard covers every path, including accidental self-test use.
  if (read_only_) {
    engine_.events().record(EventKind::REJECTED, engine_.channel(), 0, uint32_t(RejectReason::PASSIVE));
    return false;
  }
  if (selftest && engine_.registration_only()) {
    engine_.events().record(EventKind::REJECTED, engine_.channel(), 0, uint32_t(RejectReason::REGISTRATION_ONLY));
    return false;
  }
  if (parent_ == nullptr || (uart_component_ != nullptr && uart_component_->is_failed())) {
    engine_.events().record(EventKind::REJECTED, engine_.channel(), 0, uint32_t(RejectReason::TRANSPORT));
    return false;
  }
  auto result = uart::UARTFlushResult::UART_FLUSH_RESULT_FAILED;
  const bool already_faulted = transport_write_.faulted();
  bool ok = transport_write_.send(n,
      [this]() { return parent_->available_for_write(); },
      [this]() { return uart_component_ == nullptr || !uart_component_->is_failed(); },
      [this](bool drive) {
        if (direction_pin_ != nullptr) {
          direction_pin_->digital_write(drive);
          if (drive) delayMicroseconds(10);
        }
      },
      [this, d, n]() { boot_trace.mark(ExecutionStage::TX_WRITE); this->write_array(d, n); },
      [this, &result](uint32_t timeout_ms) {
        // IDF honors this local liveness limit. It is not a protocol response
        // deadline, and a timeout must never become a toggle retry.
        parent_->set_flush_timeout(timeout_ms);
        boot_trace.mark(ExecutionStage::TX_DRAIN);
        result = this->flush();
        return result == uart::UARTFlushResult::UART_FLUSH_RESULT_SUCCESS;
      });
  boot_trace.mark(ExecutionStage::TX_RELEASED);
  if (!already_faulted && transport_write_.faulted())
    ESP_LOGE(TAG, "UART transport fault; further writes disabled until reboot");
  if (!transport_write_.attempted()) {
    engine_.events().record(EventKind::REJECTED, engine_.channel(), 0, uint32_t(RejectReason::TRANSPORT));
    return false;
  }
  if (selftest || engine_.tx_cause() != TxCause::IDLE || engine_.trace_write()) {
    engine_.events().record(EventKind::UART_RETURN, n >= 5 ? d[2] : 0,
        n >= 5 ? (uint16_t(d[3]) << 8) | d[4] : 0,
        (uint32_t(selftest ? TxCause::SELFTEST : engine_.tx_cause()) << 24) |
        (uint32_t(uart_component_ != nullptr && uart_component_->is_failed()) << 23) |
        (uint32_t(result) << 16) | uint32_t(n), selftest ? 0 : engine_.tx_ref(),
        !selftest && engine_.tx_cause() == TxCause::IDLE);
  }
  tx_frames_++;
  tx_bytes_ += n;
  if (n >= 5) {
    last_tx_channel_ = d[2];
    last_tx_type_ = (uint16_t(d[3]) << 8) | d[4];
  }
  return ok;
}

void BalboaSpa::publish_command_response_(const char *request, const char *command,
                                          const char *value, uint8_t index) {
  ExecutionScope execution(boot_trace, ExecutionStage::MQTT_PUBLISH);
#ifdef USE_MQTT
  auto *client = mqtt::global_mqtt_client;
  if (client == nullptr || !client->is_connected()) return;
  if (status_topic_.empty()) {
    status_topic_ = client->get_topic_prefix();
    if (!status_topic_.empty() && status_topic_.back() != '/') status_topic_ += '/';
    status_topic_ += "status";
  }
  const auto now = engine_.events().now();
  const SpaStatus &s = engine_.status();
  const bool fresh = status_publication_.have_observation() && s.valid && bus_connected_ &&
      now.uptime_ms - status_publication_.observed_ms() <= BUS_TIMEOUT_MS;
  client->publish_json(status_topic_, [&](JsonObject root) {
    root["schema_version"] = 2;
    root["message"] = "COMMAND";
    root["command"] = command;
    root["value"] = value;
    root["index"] = index;
    root["phase"] = "PARSED";
    if (request != nullptr) root["request"] = request;
    root["boot_id"] = diagnostic_boot_id_;
    root["uptime_ms"] = now.uptime_ms;
    if (now.utc_s) root["utc_s"] = now.utc_s;
    else root["utc_s"] = nullptr;
    root["bus_connected"] = bus_connected_;
    root["fresh"] = fresh;
    root["observation_seq"] = status_publication_.observation_seq();
    if (status_publication_.have_observation()) {
      root["observed_uptime_ms"] = status_publication_.observed_ms();
      root["status_age_ms"] = now.uptime_ms - status_publication_.observed_ms();
    } else {
      root["observed_uptime_ms"] = nullptr;
      root["status_age_ms"] = nullptr;
    }
    if (!fresh) { root["status"] = nullptr; return; }
    auto state = root["status"].to<JsonObject>();
    if (s.current_temp_valid) state["current_temperature_c"] = spa_raw_to_celsius(s.current_temp_raw, s.temp_scale);
    else state["current_temperature_c"] = nullptr;
    state["target_temperature_c"] = spa_raw_to_celsius(s.target_temp_raw, s.temp_scale);
    auto pumps = state["pumps"].to<JsonArray>();
    for (auto speed : s.pumps) pumps.add(speed);
    auto lights = state["lights"].to<JsonArray>();
    for (auto on : s.lights) lights.add(on);
    auto aux = state["aux"].to<JsonArray>();
    for (auto on : s.aux) aux.add(on);
    state["blower"] = s.blower;
    state["hold"] = s.hold;
    state["priming"] = s.priming;
    state["heating"] = s.heating;
    state["heating_mode_raw"] = uint8_t(s.heating_mode);
    state["temperature_scale_raw"] = uint8_t(s.temp_scale);
    state["temperature_range_raw"] = uint8_t(s.temp_range);
    state["circulation_pump"] = s.circulation_pump;
    state["mister"] = s.mister;
    state["filter1_running"] = s.filter_running[0];
    state["filter2_running"] = s.filter_running[1];
    state["notification"] = s.notification;
    state["hour"] = s.hour;
    state["minute"] = s.minute;
  }, 0, false);
#endif
}

bool BalboaSpa::publish_config_snapshot_() {
#ifdef USE_MQTT
  auto *client = mqtt::global_mqtt_client;
  if (client == nullptr || !client->is_connected()) return false;
  if (status_topic_.empty()) {
    status_topic_ = client->get_topic_prefix();
    if (!status_topic_.empty() && status_topic_.back() != '/') status_topic_ += '/';
    status_topic_ += "status";
  }
  const auto &info = engine_.info();
  const auto &config = engine_.config();
  const auto now = engine_.events().now();
  return client->publish_json(status_topic_, [&](JsonObject root) {
    root["schema_version"] = 2;
    root["message"] = "CONFIG";
    root["value"] = "SPA";
    root["boot_id"] = diagnostic_boot_id_;
    root["uptime_ms"] = now.uptime_ms;
    root["info_valid"] = info.valid;
    root["config_valid"] = config.valid;
    if (info.valid) {
      root["model"] = info.model;
      root["version"] = info.version;
    } else {
      root["model"] = nullptr;
      root["version"] = nullptr;
    }
    auto equipment = root["equipment"].to<JsonObject>();
    auto pumps = equipment["pumps"].to<JsonArray>();
    for (auto speed_count : config.pumps) pumps.add(speed_count);
    auto lights = equipment["lights"].to<JsonArray>();
    for (auto present : config.lights) lights.add(present);
    auto aux = equipment["aux"].to<JsonArray>();
    for (auto present : config.aux) aux.add(present);
    equipment["blower"] = config.blower;
    equipment["mister"] = config.mister;
    equipment["circulation_pump"] = config.circulation_pump;
  }, 0, false);
#else
  return false;
#endif
}

void BalboaSpa::handle_mqtt_command_(const std::string &payload) {
  if (!mqtt_command_state_) return;
  std::istringstream input(payload);
  std::string command;
  input >> command;
  std::transform(command.begin(), command.end(), command.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

  if (command == "STATUS") {
    unsigned selector = 0;
    if (!(input >> selector) || (selector != 0 && selector != 1 && selector != 2 &&
                                  selector != 3 && selector != 5 && selector != 8)) {
      ESP_LOGW(TAG, "Ignoring invalid MQTT STATUS selector");
      return;
    }
    // STATUS JSON is for diagnostics; also refresh every HA entity topic so
    // Home Assistant can persist the current observed values independently.
    this->status_cb_.call();
    this->publish_status_response_(static_cast<uint8_t>(selector));
    return;
  }

  if (command == "DEBUG") {
    unsigned selector = 0;
    if (!(input >> selector) || (selector != 0 && selector != 8)) {
      ESP_LOGW(TAG, "Ignoring invalid MQTT DEBUG selector");
      return;
    }
    this->publish_debug_response_(static_cast<uint8_t>(selector));
    return;
  }

  if (command != "LIGHT") {
    ESP_LOGW(TAG, "Ignoring unsupported MQTT command");
    return;
  }

  std::string token;
  if (!(input >> token)) return;
  int index = 1;
  std::string desired;
  if (token == "1" || token == "2") {
    index = token[0] - '0';
    if (!(input >> desired)) return;
  } else {
    desired = token;
  }
  std::transform(desired.begin(), desired.end(), desired.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  if (index < 1 || index > 2 || (desired != "ON" && desired != "OFF")) {
    ESP_LOGW(TAG, "Ignoring invalid MQTT LIGHT command");
    return;
  }

  const uint32_t age = millis() - last_status_ms_;
  if (read_only_ || engine_.registration_only() || !engine_.registered() || !bus_connected_ ||
      !engine_.status().valid || age > BUS_TIMEOUT_MS) {
    ESP_LOGW(TAG, "Rejecting MQTT LIGHT command without fresh registered spa state");
    return;
  }
  engine_.set_light(static_cast<uint8_t>(index - 1), desired == "ON");
  // This is an observation at command receipt, not an ACK or proof of UART delivery.
  this->publish_command_response_(payload.c_str(), "LIGHT", desired.c_str(),
                                  static_cast<uint8_t>(index));
}

void BalboaSpa::publish_status_response_(uint8_t selector) {
#ifdef USE_MQTT
  auto *client = mqtt::global_mqtt_client;
  if (client == nullptr || !client->is_connected()) return;
  if (status_topic_.empty()) {
    status_topic_ = client->get_topic_prefix();
    if (!status_topic_.empty() && status_topic_.back() != '/') status_topic_ += '/';
    status_topic_ += "status";
  }

  const auto now = engine_.events().now();
  client->publish_json(status_topic_, [&](JsonObject root) {
    root["schema_version"] = 2;
    root["message"] = "STATUS";
    root["command"] = "STATUS";
    root["value"] = selector;
    root["boot_id"] = diagnostic_boot_id_;
    root["uptime_ms"] = now.uptime_ms;
    if (now.utc_s) root["utc_s"] = now.utc_s;
    else root["utc_s"] = nullptr;

    auto status = root["status"].to<JsonObject>();
    const bool all = selector == 0;
    if (all || selector == 1) {
      auto system = status["system"].to<JsonObject>();
#ifdef USE_ESP32
      system["reset_reason_raw"] = static_cast<int>(esp_reset_reason());
      system["free_heap"] = esp_get_free_heap_size();
      system["minimum_free_heap"] = esp_get_minimum_free_heap_size();
#endif
      system["boot_id"] = diagnostic_boot_id_;
      system["uptime_ms"] = now.uptime_ms;
    }
    if (all || selector == 2) {
      auto network = status["network"].to<JsonObject>();
#ifdef USE_ESP32
      wifi_ap_record_t ap{};
      const bool connected = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
      network["connected"] = connected;
      if (connected) {
        network["rssi_dbm"] = ap.rssi;
        network["wifi_channel"] = ap.primary;
      } else {
        network["rssi_dbm"] = nullptr;
        network["wifi_channel"] = nullptr;
      }
#else
      network["connected"] = nullptr;
      network["rssi_dbm"] = nullptr;
      network["wifi_channel"] = nullptr;
#endif
    }
    if (all || selector == 3) {
      auto mqtt_status = status["mqtt"].to<JsonObject>();
      mqtt_status["connected"] = client->is_connected();
    }
    if (all || selector == 5) {
      auto spa = status["spa"].to<JsonObject>();
      const SpaStatus &s = engine_.status();
      const bool fresh = status_publication_.have_observation() && s.valid && bus_connected_ &&
          now.uptime_ms - status_publication_.observed_ms() <= BUS_TIMEOUT_MS;
      spa["bus_connected"] = bus_connected_;
      spa["fresh"] = fresh;
      spa["observation_seq"] = status_publication_.observation_seq();
      if (status_publication_.have_observation()) {
        spa["observed_uptime_ms"] = status_publication_.observed_ms();
        spa["status_age_ms"] = now.uptime_ms - status_publication_.observed_ms();
      } else {
        spa["observed_uptime_ms"] = nullptr;
        spa["status_age_ms"] = nullptr;
      }
      if (!fresh) {
        spa["state"] = nullptr;
      } else {
        auto state = spa["state"].to<JsonObject>();
        if (s.current_temp_valid) state["current_temperature_c"] = spa_raw_to_celsius(s.current_temp_raw, s.temp_scale);
        else state["current_temperature_c"] = nullptr;
        state["target_temperature_c"] = spa_raw_to_celsius(s.target_temp_raw, s.temp_scale);
        auto pumps = state["pumps"].to<JsonArray>();
        for (auto speed : s.pumps) pumps.add(speed);
        auto lights = state["lights"].to<JsonArray>();
        for (auto on : s.lights) lights.add(on);
        auto aux = state["aux"].to<JsonArray>();
        for (auto on : s.aux) aux.add(on);
        state["blower"] = s.blower;
        state["hold"] = s.hold;
        state["priming"] = s.priming;
        state["heating"] = s.heating;
        state["heating_mode_raw"] = uint8_t(s.heating_mode);
        state["temperature_scale_raw"] = uint8_t(s.temp_scale);
        state["temperature_range_raw"] = uint8_t(s.temp_range);
        state["circulation_pump"] = s.circulation_pump;
        state["mister"] = s.mister;
        state["filter1_running"] = s.filter_running[0];
        state["filter2_running"] = s.filter_running[1];
        state["notification"] = s.notification;
        state["hour"] = s.hour;
        state["minute"] = s.minute;
      }
    }
    if (all || selector == 8) {
      auto rs485 = status["rs485"].to<JsonObject>();
      const auto &d = engine_.diagnostics();
      rs485["read_only"] = read_only_;
      rs485["uart_selftest"] = uart_selftest_;
      rs485["registered"] = engine_.registered();
      rs485["registration_only"] = engine_.registration_only();
      rs485["channel"] = engine_.channel();
      rs485["rx_frames"] = engine_.frames_decoded();
      rs485["new_client_cts"] = d.new_client_cts;
      rs485["assignments"] = d.assignments;
      rs485["last_assignment"] = d.last_assignment;
      rs485["channel_adoptions"] = d.channel_adoptions;
      rs485["channel_restores"] = d.channel_restores;
      rs485["channel_stale"] = d.channel_stale;
      rs485["ready"] = d.ready;
      rs485["ready_ours"] = d.ready_ours;
      rs485["last_ready_channel"] = d.last_ready_channel;
      auto polls = rs485["ready_by_channel"].to<JsonObject>();
      for (unsigned ch = 0; ch <= channel::MAX; ch++) {
        if (d.ready_by_channel[ch] == 0) continue;
        char key[3];
        snprintf(key, sizeof(key), "%02X", ch);
        polls[key] = d.ready_by_channel[ch];
      }
      rs485["tx_frames"] = tx_frames_;
      rs485["tx_bytes"] = tx_bytes_;
      if (tx_frames_ != 0 || tx_bytes_ != 0) {
        rs485["last_tx_channel"] = last_tx_channel_;
        rs485["last_tx_type"] = last_tx_type_;
      } else {
        rs485["last_tx_channel"] = nullptr;
        rs485["last_tx_type"] = nullptr;
      }
    }
  }, 0, false);
#else
  (void) selector;
#endif
}

void BalboaSpa::publish_debug_response_(uint8_t selector) {
#ifdef USE_MQTT
  auto *client = mqtt::global_mqtt_client;
  if (client == nullptr || !client->is_connected()) return;
  if (status_topic_.empty()) {
    status_topic_ = client->get_topic_prefix();
    if (!status_topic_.empty() && status_topic_.back() != '/') status_topic_ += '/';
    status_topic_ += "status";
  }
  const auto now = engine_.events().now();
  client->publish_json(status_topic_, [&](JsonObject root) {
    root["schema_version"] = 2;
    root["message"] = "DEBUG";
    root["command"] = "DEBUG";
    root["value"] = selector;
    root["boot_id"] = diagnostic_boot_id_;
    root["uptime_ms"] = now.uptime_ms;
    auto debug = root["debug"].to<JsonObject>();
    debug["mqtt_connected"] = client->is_connected();
    if (selector == 0) {
#ifdef USE_ESP32
      debug["reset_reason_raw"] = static_cast<int>(esp_reset_reason());
      debug["free_heap"] = esp_get_free_heap_size();
      debug["minimum_free_heap"] = esp_get_minimum_free_heap_size();
#endif
      debug["boot_number"] = boot_trace.boot_number;
      debug["stage"] = boot_trace.stage;
      debug["frame_type"] = boot_trace.frame_type;
      debug["frame_length"] = boot_trace.length;
      debug["fault_boot"] = boot_trace.fault_boot;
      debug["fault_reason"] = boot_trace.fault_reason;
      debug["fault_stage"] = boot_trace.fault_stage;
      debug["fault_frame_type"] = boot_trace.fault_frame_type;
      debug["fault_length"] = boot_trace.fault_length;
      debug["transport_faulted"] = transport_write_.faulted();
    } else {
      const auto &events = engine_.events();
      debug["events_enabled"] = events.enabled();
      debug["critical_overwritten"] = events.lost();
      debug["sample_overwritten"] = events.sample_lost();
      debug["polls_coalesced"] = events.coalesced();
      debug["other_frames_coalesced"] = events.other_frames_coalesced();
      debug["pending"] = events.pending();
      BusEvent batch[EventRecorder::BATCH_CAP];
      const size_t count = events.peek_batch(batch, EventRecorder::BATCH_CAP);
      auto items = debug["events"].to<JsonArray>();
      for (size_t i = 0; i < count; i++) {
        auto item = items.add<JsonObject>();
        item["seq"] = batch[i].seq;
        item["uptime_ms"] = batch[i].time.uptime_ms;
        if (batch[i].time.utc_s) item["utc_s"] = batch[i].time.utc_s;
        else item["utc_s"] = nullptr;
        item["kind"] = EventRecorder::name(batch[i].kind);
        item["channel"] = batch[i].channel;
        item["frame_type"] = batch[i].frame_type;
        item["detail"] = batch[i].detail;
        item["ref"] = batch[i].ref;
      }
    }
  }, 0, false);
#else
  (void) selector;
#endif
}

void BalboaSpa::maybe_sync_time_() {
#ifdef USE_TIME
  if (time_ == nullptr || read_only_ || engine_.registration_only()) return;
  auto now = time_->now();
  if (!now.is_valid()) return;
  uint32_t ms = millis();
  if (last_time_sync_ != 0 && (ms - last_time_sync_) < 60000UL) return;
  const SpaStatus &s = engine_.status();
  int now_min = now.hour * 60 + now.minute;
  int spa_min = s.hour * 60 + s.minute;
  int diff = (now_min - spa_min + 1440) % 1440;
  if (diff > 720) diff = 1440 - diff;
  if (diff > 1) {
    ESP_LOGI(TAG, "Syncing spa clock %02d:%02d -> %02d:%02d", s.hour, s.minute, now.hour, now.minute);
    engine_.set_time(now.hour, now.minute, s.twenty_four_hour);
    last_time_sync_ = ms;
  }
#endif
}

void BalboaSpa::dump_config() {
  ESP_LOGCONFIG(TAG, "Balboa Spa:");
  ESP_LOGCONFIG(TAG, "  read_only: %s", read_only_ ? "YES" : "NO");
  LOG_PIN("  Direction pin: ", direction_pin_);
  if (engine_.registered())
    ESP_LOGCONFIG(TAG, "  bus channel: 0x%02X", engine_.channel());
  else
    ESP_LOGCONFIG(TAG, "  bus channel: 0x%02X (unverified; read-only stays silent)", engine_.channel());
  this->check_uart_settings(115200);
}

}  // namespace balboa_spa
}  // namespace esphome
