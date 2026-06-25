#include "balboa_binary_sensor.h"

namespace esphome {
namespace balboa_spa {

void BalboaBinarySensor::setup() {
  parent_->add_on_status_callback([this]() { this->update_from_spa_(); });
}

void BalboaBinarySensor::update_from_spa_() {
  const SpaStatus &s = parent_->status();
  if (!s.valid) return;
  bool v = false;
  switch (type_) {
    case HEATING: v = s.heating; break;
    case PRIMING: v = s.priming; break;
    case CIRCULATION_PUMP: v = s.circulation_pump; break;
    case FILTER1_RUNNING: v = s.filter_running[0]; break;
    case FILTER2_RUNNING: v = s.filter_running[1]; break;
  }
  this->publish_state(v);
}

}  // namespace balboa_spa
}  // namespace esphome
