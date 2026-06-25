#include "crc.h"

namespace esphome {
namespace balboa_spa {

uint8_t balboa_crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0x02;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      if (crc & 0x80)
        crc = (uint8_t)((crc << 1) ^ 0x07);
      else
        crc = (uint8_t)(crc << 1);
    }
  }
  return (uint8_t)(crc ^ 0x02);
}

}  // namespace balboa_spa
}  // namespace esphome
