#include "balboa_text_sensor.h"

namespace esphome {
namespace balboa_spa {

static const char *notification_str(uint8_t code) {
  switch (code) {
    case 0x0a: return "ph";
    case 0x04: return "filter";
    case 0x09: return "sanitizer";
    default: return "none";
  }
}

void BalboaTextSensor::setup() {
  parent_->add_on_status_callback([this]() { this->update_(); });
  parent_->add_on_config_callback([this]() { this->update_(); });
}

void BalboaTextSensor::update_() {
  std::string v;
  switch (type_) {
    case MODEL: v = parent_->info().valid ? parent_->info().model : ""; break;
    case VERSION: v = parent_->info().valid ? parent_->info().version : ""; break;
    case NOTIFICATION:
      v = parent_->status().valid ? notification_str(parent_->status().notification) : "none";
      break;
  }
  if (v.empty() || v == last_) return;
  last_ = v;
  this->publish_state(v);
}

}  // namespace balboa_spa
}  // namespace esphome
