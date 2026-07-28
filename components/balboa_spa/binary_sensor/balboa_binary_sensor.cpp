#include "balboa_binary_sensor.h"

namespace esphome {
namespace balboa_spa {

void BalboaBinarySensor::setup() {
  parent_->add_on_status_callback([this]() { this->update_from_spa_(); });
  if (type_ == BUS_CONNECTED) {
    // Also driven by liveness transitions, which fire when no Status arrives —
    // precisely when the status callback cannot.
    parent_->add_on_bus_callback([this]() { this->update_from_spa_(); });
    this->publish_state(false);   // assume down until the spa proves otherwise
  }
}

void BalboaBinarySensor::update_from_spa_() {
  if (type_ == BUS_CONNECTED) {
    this->publish_state(parent_->bus_connected());
    return;
  }
  const SpaStatus &s = parent_->status();
  if (!s.valid) return;
  bool v = false;
  switch (type_) {
    case HEATING: v = s.heating; break;
    case PRIMING: v = s.priming; break;
    case CIRCULATION_PUMP: v = s.circulation_pump; break;
    case FILTER1_RUNNING: v = s.filter_running[0]; break;
    case FILTER2_RUNNING: v = s.filter_running[1]; break;
    case BUS_CONNECTED: return;   // handled above
  }
  this->publish_state(v);
}

}  // namespace balboa_spa
}  // namespace esphome
