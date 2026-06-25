#include "temperature.h"
#include <cmath>

namespace esphome {
namespace balboa_spa {

float spa_raw_to_native(uint8_t raw, TempScale scale) {
  return (scale == TempScale::CELSIUS) ? (raw / 2.0f) : (float) raw;
}

float spa_raw_to_celsius(uint8_t raw, TempScale scale) {
  if (scale == TempScale::CELSIUS) return raw / 2.0f;
  return (raw - 32.0f) * 5.0f / 9.0f;
}

uint8_t celsius_to_spa_raw(float celsius, TempScale scale) {
  if (scale == TempScale::CELSIUS)
    return (uint8_t) std::lround(celsius * 2.0f);
  return (uint8_t) std::lround(celsius * 9.0f / 5.0f + 32.0f);
}

}  // namespace balboa_spa
}  // namespace esphome
