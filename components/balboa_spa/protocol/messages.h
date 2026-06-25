#pragma once
#include "frame.h"
#include "types.h"

namespace esphome {
namespace balboa_spa {

// Message type bytes (TYPE0 TYPE1).
namespace msg {
constexpr uint8_t STATUS0 = 0xaf, STATUS1 = 0x13;
constexpr uint8_t READY0 = 0xbf, READY1 = 0x06;
constexpr uint8_t NEW_CLIENT0 = 0xbf, NEW_CLIENT1 = 0x00;
constexpr uint8_t CTRL_CFG0 = 0xbf, CTRL_CFG1 = 0x24;   // info (model/version)
constexpr uint8_t CTRL_CFG2_0 = 0xbf, CTRL_CFG2_1 = 0x2e;  // accessory inventory
constexpr uint8_t FILTER0 = 0xbf, FILTER1 = 0x23;
}  // namespace msg

bool frame_is(const ParsedFrame &f, uint8_t t0, uint8_t t1);
inline bool is_ready(const ParsedFrame &f) { return frame_is(f, msg::READY0, msg::READY1); }
inline bool is_new_client_cts(const ParsedFrame &f) { return frame_is(f, msg::NEW_CLIENT0, msg::NEW_CLIENT1); }

bool decode_status(const ParsedFrame &f, SpaStatus *out);
bool decode_control_config(const ParsedFrame &f, SpaInfo *out);
bool decode_control_config2(const ParsedFrame &f, SpaConfig *out);
bool decode_filter_cycles(const ParsedFrame &f, FilterCyclesData *out);
size_t encode_filter_cycles(uint8_t *out, const FilterCyclesData &fc);

}  // namespace balboa_spa
}  // namespace esphome
