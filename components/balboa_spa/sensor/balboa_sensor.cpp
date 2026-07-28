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
    // Always publish Celsius, matching the climate entity and ESPHome/HA
    // convention. Publishing the spa's native scale here was wrong twice over:
    // it disagreed with climate (same value, two units), and the entity's
    // unit_of_measurement is static while the spa's scale can change at runtime
    // — so switching the spa to Celsius produced Celsius values labelled °F.
    // Front-ends convert for display using select/spa_temperature_scale.
    case CURRENT_TEMPERATURE:
      if (s.current_temp_valid)
        this->publish_state(spa_raw_to_celsius(s.current_temp_raw, s.temp_scale));
      break;
    case TARGET_TEMPERATURE:
      this->publish_state(spa_raw_to_celsius(s.target_temp_raw, s.temp_scale));
      break;
  }
}

}  // namespace balboa_spa
}  // namespace esphome
