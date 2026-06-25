#include "balboa_fan.h"

namespace esphome {
namespace balboa_spa {

void BalboaFan::setup() {
  parent_->add_on_status_callback([this]() { this->update_from_spa_(); });
}

fan::FanTraits BalboaFan::get_traits() {
  return fan::FanTraits(false, speed_count_ > 1, false, speed_count_);
}

uint8_t BalboaFan::spa_speed_() {
  const SpaStatus &s = parent_->status();
  return (type_ == FAN_PUMP) ? s.pumps[index_] : s.blower;
}

void BalboaFan::update_from_spa_() {
  if (!parent_->status().valid) return;
  uint8_t spd = this->spa_speed_();
  this->state = spd != 0;
  this->speed = spd == 0 ? 1 : spd;  // ESPHome speed is 1..count when on
  this->publish_state();
}

void BalboaFan::control(const fan::FanCall &call) {
  uint8_t desired;
  if (call.get_state().has_value() && !*call.get_state()) {
    desired = 0;
  } else {
    int spd = call.get_speed().has_value() ? *call.get_speed() : (this->speed > 0 ? this->speed : 1);
    if (spd < 1) spd = 1;
    if (spd > speed_count_) spd = speed_count_;
    desired = (uint8_t) spd;
  }
  if (type_ == FAN_PUMP)
    parent_->engine().set_pump(index_, desired);
  else
    parent_->engine().set_blower(desired);
}

}  // namespace balboa_spa
}  // namespace esphome
