#pragma once
#include <cstdint>
#include "types.h"

namespace esphome {
namespace balboa_spa {

float spa_raw_to_celsius(uint8_t raw, TempScale scale);
// The spa's own scale, for display/diagnostics ONLY — the value's unit changes
// with the spa's temperature-scale setting, so it must never be published to an
// entity with a fixed unit_of_measurement. Use spa_raw_to_celsius() for that.
float spa_raw_to_native(uint8_t raw, TempScale scale);
uint8_t celsius_to_spa_raw(float celsius, TempScale scale);

}  // namespace balboa_spa
}  // namespace esphome
