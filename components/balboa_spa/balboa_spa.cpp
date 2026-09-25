#include "balboa_spa.h"
#include "esphome/core/log.h"
#include <cstring>

namespace esphome {
namespace balboa_spa {

static const char *const TAG = "balboa_spa";

void BalboaSpa::setup() {
  if (direction_pin_ != nullptr) {
    // Receive by default. Left floating, a MAX485's DE/RE can enable its driver
    // and hold the shared bus, and the receiver never passes anything to RX.
    direction_pin_->setup();
    direction_pin_->digital_write(false);
  }
  engine_.set_read_only(read_only_);
  engine_.set_write_fn([this](const uint8_t *d, size_t n) { this->bus_write_(d, n); });

  // Reuse the channel the controller gave us previously. The spa polls an
  // assigned channel forever, so resuming it avoids leaking a new one on every
  // reboot (an out-of-range or empty value falls through to a fresh handshake).
  this->channel_pref_ = global_preferences->make_preference<uint8_t>(fnv1_hash("balboa_spa_channel"));
  uint8_t saved = 0;
  if (this->channel_pref_.load(&saved)) {
    engine_.restore_channel(saved);
    if (engine_.registered())
      ESP_LOGI(TAG, "Resuming saved bus channel 0x%02X", engine_.channel());
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
    const SpaStatus &st = this->engine_.status();
    uint32_t now = millis();
    if (std::memcmp(&st, &this->last_published_status_, sizeof(SpaStatus)) != 0 ||
        (now - this->last_status_publish_ms_) > STATUS_REFRESH_MS) {
      std::memcpy(&this->last_published_status_, &st, sizeof(SpaStatus));
      this->last_status_publish_ms_ = now;
      this->status_cb_.call();
    }
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

    this->bus_write_(pattern, sizeof(pattern));
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

void BalboaSpa::bus_write_(const uint8_t *d, size_t n) {
  if (direction_pin_ != nullptr) {
    direction_pin_->digital_write(true);
    delayMicroseconds(10);   // MAX485 driver enable is well under 1 us
  }
  this->write_array(d, n);
  // flush() waits for the last stop bit to leave the shifter, so the driver is
  // never released mid-byte.
  this->flush();
  if (direction_pin_ != nullptr)
    direction_pin_->digital_write(false);
}

void BalboaSpa::maybe_sync_time_() {
#ifdef USE_TIME
  if (time_ == nullptr || read_only_) return;
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
    ESP_LOGCONFIG(TAG, "  bus channel: unassigned (will negotiate)");
  this->check_uart_settings(115200);
}

}  // namespace balboa_spa
}  // namespace esphome
