#include "frame.h"
#include "crc.h"

namespace esphome {
namespace balboa_spa {

static constexpr uint8_t DELIM = 0x7e;

ScanResult scan_frame(const uint8_t *buf, size_t len, ParsedFrame *out, size_t *consumed) {
  size_t offset = 0;
  while (true) {
    // Need at least 5 bytes from offset to inspect a frame header+.
    if (offset + 5 > len) {
      // Discard anything before a trailing partial that begins with DELIM.
      // Find the last DELIM at/after offset to preserve as a partial.
      size_t keep = offset;
      while (keep < len && buf[keep] != DELIM) keep++;
      *consumed = keep;
      return ScanResult::NEED_MORE;
    }
    if (buf[offset] != DELIM) { offset++; continue; }

    uint8_t length = buf[offset + 1];  // LEN byte
    if (length < 5 || length >= DELIM) { offset++; continue; }

    // Total frame size = 1 (start) + length + 1 (end). Need length+2 bytes from offset.
    if (offset + (size_t)length + 2 > len) {
      *consumed = offset;  // discard leading garbage, keep partial frame
      return ScanResult::NEED_MORE;
    }
    if (buf[offset + length + 1] != DELIM) { offset++; continue; }

    // CRC over bytes [offset+1 .. offset+length-1] (LEN..last payload byte).
    uint8_t crc = balboa_crc8(buf + offset + 1, (size_t)length - 1);
    if (crc != buf[offset + length]) { offset++; continue; }

    out->src = buf[offset + 2];
    out->type0 = buf[offset + 3];
    out->type1 = buf[offset + 4];
    out->payload = buf + offset + 5;
    out->payload_len = (uint8_t)(length - 5);
    *consumed = offset + (size_t)length + 2;
    return ScanResult::FRAME;
  }
}

size_t build_frame(uint8_t *out, uint8_t src, uint8_t type0, uint8_t type1,
                   const uint8_t *payload, uint8_t payload_len) {
  uint8_t length = (uint8_t)(payload_len + 5);
  out[0] = DELIM;
  out[1] = length;
  out[2] = src;
  out[3] = type0;
  out[4] = type1;
  for (uint8_t i = 0; i < payload_len; i++) out[5 + i] = payload[i];
  uint8_t crc = balboa_crc8(out + 1, (size_t)length - 1);
  out[5 + payload_len] = crc;
  out[6 + payload_len] = DELIM;
  return (size_t)payload_len + 7;
}

}  // namespace balboa_spa
}  // namespace esphome
