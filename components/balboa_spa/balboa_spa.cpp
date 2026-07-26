#include "balboa_spa.h"
#include "esphome/core/log.h"

namespace esphome {
namespace balboa_spa {

static const char *const TAG = "balboa_spa";

void BalboaSpa::setup() {
  engine_.set_read_only(read_only_);
  engine_.set_write_fn([this](const uint8_t *d, size_t n) { this->write_array(d, n); });

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
}

void BalboaSpa::loop() {
  int avail = this->available();
  while (avail > 0) {
    int n = avail > (int) sizeof(rx_chunk_) ? (int) sizeof(rx_chunk_) : avail;
    this->read_array(rx_chunk_, n);
    engine_.feed(rx_chunk_, n);
    avail -= n;
  }
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
  if (engine_.registered())
    ESP_LOGCONFIG(TAG, "  bus channel: 0x%02X", engine_.channel());
  else
    ESP_LOGCONFIG(TAG, "  bus channel: unassigned (will negotiate)");
  this->check_uart_settings(115200);
}

}  // namespace balboa_spa
}  // namespace esphome
