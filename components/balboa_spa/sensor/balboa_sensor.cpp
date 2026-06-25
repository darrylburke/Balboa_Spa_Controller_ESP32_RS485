#include "balboa_sensor.h"
#include "protocol/temperature.h"

namespace esphome {
namespace balboa_spa {

void BalboaSensor::setup() {
  parent_->add_on_status_callback([this]() { this->update_from_spa_(); });
}

void BalboaSensor::update_from_spa_() {
  const SpaStatus &s = parent_->status();
  if (!s.valid) return;
  switch (type_) {
    case CURRENT_TEMPERATURE:
      if (s.current_temp_valid)
        this->publish_state(spa_raw_to_native(s.current_temp_raw, s.temp_scale));
      break;
    case TARGET_TEMPERATURE:
      this->publish_state(spa_raw_to_native(s.target_temp_raw, s.temp_scale));
      break;
  }
}

}  // namespace balboa_spa
}  // namespace esphome
