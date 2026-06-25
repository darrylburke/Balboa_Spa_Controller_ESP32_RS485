#include "balboa_switch.h"

namespace esphome {
namespace balboa_spa {

void BalboaSwitch::setup() {
  parent_->add_on_status_callback([this]() { this->update_from_spa_(); });
}

bool BalboaSwitch::current_spa_state_() {
  const SpaStatus &s = parent_->status();
  switch (type_) {
    case SW_LIGHT: return s.lights[index_];
    case SW_AUX: return s.aux[index_];
    case SW_MISTER: return s.mister;
    case SW_HOLD: return s.hold;
    case SW_PUMP_ONOFF: return s.pumps[index_] != 0;
    case SW_BLOWER_ONOFF: return s.blower != 0;
  }
  return false;
}

void BalboaSwitch::update_from_spa_() {
  if (!parent_->status().valid) return;
  this->publish_state(this->current_spa_state_());
}

void BalboaSwitch::write_state(bool state) {
  auto &e = parent_->engine();
  switch (type_) {
    case SW_LIGHT: e.set_light(index_, state); break;
    case SW_AUX: e.set_aux(index_, state); break;
    case SW_MISTER: e.set_mister(state); break;
    case SW_HOLD: e.set_hold(state); break;
    case SW_PUMP_ONOFF: e.set_pump(index_, state ? 1 : 0); break;
    case SW_BLOWER_ONOFF: e.set_blower(state ? 1 : 0); break;
  }
}

}  // namespace balboa_spa
}  // namespace esphome
