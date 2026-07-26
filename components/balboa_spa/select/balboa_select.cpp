#include "balboa_select.h"

namespace esphome {
namespace balboa_spa {

void BalboaSelect::setup() {
  parent_->add_on_status_callback([this]() { this->update_from_spa_(); });
}

void BalboaSelect::update_from_spa_() {
  const SpaStatus &s = parent_->status();
  if (!s.valid) return;
  std::string v;
  switch (type_) {
    case SEL_HEATING_MODE:
      // The select exposes only ready/rest; READY_IN_REST reports as "ready".
      v = (s.heating_mode == HeatingMode::REST) ? "rest" : "ready";
      break;
    case SEL_TEMP_RANGE:
      v = (s.temp_range == TempRange::HIGH) ? "high" : "low";
      break;
    case SEL_TEMP_SCALE:
      v = (s.temp_scale == TempScale::CELSIUS) ? "celsius" : "fahrenheit";
      break;
  }
  this->publish_state(v);
}

void BalboaSelect::control(const std::string &value) {
  auto &e = parent_->engine();
  switch (type_) {
    case SEL_HEATING_MODE:
      e.set_heating_mode(value == "rest" ? HeatingMode::REST : HeatingMode::READY);
      break;
    case SEL_TEMP_RANGE:
      e.set_temperature_range(value == "low" ? TempRange::LOW : TempRange::HIGH);
      break;
    case SEL_TEMP_SCALE:
      e.set_temperature_scale(value == "celsius" ? TempScale::CELSIUS : TempScale::FAHRENHEIT);
      break;
  }
}

}  // namespace balboa_spa
}  // namespace esphome
