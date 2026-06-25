#pragma once
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace balboa_spa {

struct ParsedFrame {
  uint8_t src;
  uint8_t type0;
  uint8_t type1;
  const uint8_t *payload;  // points into the caller's buffer
  uint8_t payload_len;
};

enum class ScanResult { FRAME, NEED_MORE };

// Scan buf for the next valid frame. See header doc in plan Task 3 Interfaces.
ScanResult scan_frame(const uint8_t *buf, size_t len, ParsedFrame *out, size_t *consumed);

// Build a frame into out (caller ensures capacity >= payload_len + 7). Returns total length.
size_t build_frame(uint8_t *out, uint8_t src, uint8_t type0, uint8_t type1,
                   const uint8_t *payload, uint8_t payload_len);

}  // namespace balboa_spa
}  // namespace esphome
