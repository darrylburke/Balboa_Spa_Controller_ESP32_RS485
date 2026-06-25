#pragma once
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace balboa_spa {

// CRC-8: poly 0x07, init 0x02, final XOR 0x02, no reflection.
uint8_t balboa_crc8(const uint8_t *data, size_t len);

}  // namespace balboa_spa
}  // namespace esphome
