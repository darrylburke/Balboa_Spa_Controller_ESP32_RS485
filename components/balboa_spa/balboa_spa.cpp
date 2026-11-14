#include "balboa_spa.h"
#include "esphome/core/log.h"
#include <cstring>
#include <ctime>
#include "protocol/temperature.h"
#ifdef USE_MQTT
#include "esphome/components/mqtt/mqtt_client.h"
#endif

namespace esphome {
namespace balboa_spa {

static const char *const TAG = "balboa_spa";

void BalboaSpa::setup() {
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

  // Load the previous address for diagnostics, never as proof of ownership.
  // Active operation must register again before responding to any channel poll.
  this->channel_pref_ = global_preferences->make_preference<uint8_t>(fnv1_hash("balboa_spa_channel"));
  uint8_t saved = 0;
  if (this->channel_pref_.load(&saved)) {
    engine_.restore_channel(saved);
    if (engine_.channel() == saved && saved != channel::UNASSIGNED)
      ESP_LOGI(TAG, "Saved bus channel 0x%02X (unverified; fresh registration required)", saved);
    else
      ESP_LOGW(TAG, "Saved bus channel 0x%02X is invalid; will negotiate a new one", saved);
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
  engine_.on_status_update = [this]() {
    this->last_status_ms_ = millis();
    if (!this->bus_connected_) {
      this->bus_connected_ = true;
      ESP_LOGI(TAG, "RS-485 bus connected (Status received)");
      this->bus_cb_.call();
    }
    // The spa repeats Status several times a second. Sensors, climate, fan and
    // select publish on every call, so fanning out each one flooded MQTT (12k
    // dropped messages/10s) and kept knocking the broker link over. Notify
    // entities only on change, plus a periodic refresh.
    if (this->status_publication_.observe(this->engine_.status(), millis_64()))
      this->status_cb_.call();
    this->maybe_sync_time_();
  };
  engine_.on_config_update = [this]() {
    this->config_cb_.call();
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
  // Serialization/publication runs outside feed()/process_frame(). No bus timer,
  // registration or response opportunity is changed by this presentation timer.
  this->set_interval("observed_state_snapshot", StatusPublication::SNAPSHOT_MS,
                     [this]() { this->publish_state_snapshot_(); });
#endif
}

void BalboaSpa::loop() {
  int avail = this->available();
  while (avail > 0) {
    int n = avail > (int) sizeof(rx_chunk_) ? (int) sizeof(rx_chunk_) : avail;
    this->read_array(rx_chunk_, n);
    rx_bytes_ += (uint32_t) n;
    engine_.feed(rx_chunk_, n);
    avail -= n;
  }

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

  // Detect the bus going quiet. Every published state topic is retained, so
  // without this the last values sit on the broker looking current while the
  // spa is actually unreachable.
  if (this->bus_connected_ && (millis() - this->last_status_ms_) > BUS_TIMEOUT_MS) {
    this->bus_connected_ = false;
    ESP_LOGW(TAG, "RS-485 bus lost (no Status for %ums); published values are now stale",
             (unsigned) BUS_TIMEOUT_MS);
    this->bus_cb_.call();
  }
}

bool BalboaSpa::bus_write_(const uint8_t *d, size_t n, bool selftest) {
  // Final transport guard covers every path, including accidental self-test use.
  if (read_only_) {
    engine_.events().record(EventKind::REJECTED, engine_.channel(), 0, uint32_t(RejectReason::PASSIVE));
    return false;
  }
  if (selftest && engine_.registration_only()) {
    engine_.events().record(EventKind::REJECTED, engine_.channel(), 0, uint32_t(RejectReason::REGISTRATION_ONLY));
    return false;
  }
  if (uart_component_ != nullptr && uart_component_->is_failed()) {
    engine_.events().record(EventKind::REJECTED, engine_.channel(), 0, uint32_t(RejectReason::TRANSPORT));
    return false;
  }
  if (direction_pin_ != nullptr) {
    direction_pin_->digital_write(true);
    delayMicroseconds(10);   // MAX485 driver enable is well under 1 us
  }
  this->write_array(d, n);
  // flush() waits for the last stop bit to leave the shifter, so the driver is
  // never released mid-byte.
  auto result = this->flush();
  if (direction_pin_ != nullptr)
    direction_pin_->digital_write(false);
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
  return result == uart::UARTFlushResult::UART_FLUSH_RESULT_SUCCESS &&
      (uart_component_ == nullptr || !uart_component_->is_failed());
}

void BalboaSpa::publish_state_snapshot_() {
#ifdef USE_MQTT
  auto *client = mqtt::global_mqtt_client;
  if (client == nullptr || !client->is_connected()) return;
  if (state_snapshot_topic_.empty()) {
    state_snapshot_topic_ = client->get_topic_prefix();
    if (!state_snapshot_topic_.empty() && state_snapshot_topic_.back() != '/') state_snapshot_topic_ += '/';
    state_snapshot_topic_ += "state";
  }
  const auto now = engine_.events().now();
  const SpaStatus &s = engine_.status();
  const bool fresh = status_publication_.snapshot_fresh(s, bus_connected_, now.uptime_ms);
  bool accepted = client->publish_json(state_snapshot_topic_, [&](JsonObject root) {
    root["schema_version"] = 1;
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
  if (accepted) status_publication_.snapshot_published();
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
