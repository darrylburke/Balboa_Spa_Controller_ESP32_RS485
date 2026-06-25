#include "balboa_number.h"

namespace esphome {
namespace balboa_spa {

void BalboaNumber::setup() {
  parent_->add_on_config_callback([this]() { this->update_from_spa_(); });
}

void BalboaNumber::update_from_spa_() {
  const FilterCyclesData &fc = parent_->filter_cycles();
  if (!fc.valid) return;
  float v = 0;
  switch (type_) {
    case C1_START_HOUR: v = fc.c1_start_hour; break;
    case C1_START_MINUTE: v = fc.c1_start_minute; break;
    case C1_DURATION: v = fc.c1_duration_min; break;
    case C2_START_HOUR: v = fc.c2_start_hour; break;
    case C2_START_MINUTE: v = fc.c2_start_minute; break;
    case C2_DURATION: v = fc.c2_duration_min; break;
  }
  this->publish_state(v);
}

void BalboaNumber::control(float value) {
  FilterCyclesData fc = parent_->filter_cycles();
  if (!fc.valid) return;
  uint16_t iv = (uint16_t) value;
  switch (type_) {
    case C1_START_HOUR: fc.c1_start_hour = (uint8_t) iv; break;
    case C1_START_MINUTE: fc.c1_start_minute = (uint8_t) iv; break;
    case C1_DURATION: fc.c1_duration_min = iv; break;
    case C2_START_HOUR: fc.c2_start_hour = (uint8_t) iv; break;
    case C2_START_MINUTE: fc.c2_start_minute = (uint8_t) iv; break;
    case C2_DURATION: fc.c2_duration_min = iv; break;
  }
  fc.c2_enabled = fc.c2_duration_min > 0;
  parent_->engine().update_filter_cycles(fc);
  this->publish_state(value);
}

}  // namespace balboa_spa
}  // namespace esphome
