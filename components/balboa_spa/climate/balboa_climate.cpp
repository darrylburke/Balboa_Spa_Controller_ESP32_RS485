#include "balboa_climate.h"
#include "protocol/temperature.h"

namespace esphome {
namespace balboa_spa {

void BalboaClimate::setup() {
  parent_->add_on_status_callback([this]() { this->update_from_spa_(); });
}

climate::ClimateTraits BalboaClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_supports_current_temperature(true);
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});
  traits.set_visual_min_temperature(10.0);   // 50 F
  traits.set_visual_max_temperature(40.0);   // 104 F
  traits.set_visual_temperature_step(0.5);
  traits.set_supports_action(true);
  return traits;
}

void BalboaClimate::update_from_spa_() {
  const SpaStatus &s = parent_->status();
  if (!s.valid) return;
  if (s.current_temp_valid)
    this->current_temperature = spa_raw_to_celsius(s.current_temp_raw, s.temp_scale);
  this->target_temperature = spa_raw_to_celsius(s.target_temp_raw, s.temp_scale);
  // The spa heater is always "on" (it heats to setpoint); model OFF only in Rest with no demand.
  this->mode = climate::CLIMATE_MODE_HEAT;
  this->action = s.heating ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_IDLE;
  this->publish_state();
}

void BalboaClimate::control(const climate::ClimateCall &call) {
  if (call.get_target_temperature().has_value()) {
    const SpaStatus &s = parent_->status();
    if (!s.valid) return;
    float c = *call.get_target_temperature();
    uint8_t raw = celsius_to_spa_raw(c, s.temp_scale);
    parent_->engine().set_target_temperature_raw(raw);
  }
  // mode changes are not separately actionable (heater follows setpoint); ignore.
}

}  // namespace balboa_spa
}  // namespace esphome
