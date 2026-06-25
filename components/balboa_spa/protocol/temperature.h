#pragma once
#include <cstdint>
#include "types.h"

namespace esphome {
namespace balboa_spa {

float spa_raw_to_celsius(uint8_t raw, TempScale scale);
float spa_raw_to_native(uint8_t raw, TempScale scale);
uint8_t celsius_to_spa_raw(float celsius, TempScale scale);

}  // namespace balboa_spa
}  // namespace esphome
