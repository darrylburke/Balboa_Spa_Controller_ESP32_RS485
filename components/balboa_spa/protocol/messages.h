#pragma once
#include "frame.h"
#include "types.h"

namespace esphome {
namespace balboa_spa {

// Message type bytes (TYPE0 TYPE1).
namespace msg {
constexpr uint8_t STATUS0 = 0xaf, STATUS1 = 0x13;
constexpr uint8_t READY0 = 0xbf, READY1 = 0x06;
constexpr uint8_t NEW_CLIENT0 = 0xbf, NEW_CLIENT1 = 0x00;  // "any new clients?"
constexpr uint8_t ID_REQ0 = 0xbf, ID_REQ1 = 0x01;          // client -> request a channel
constexpr uint8_t ID_ASSIGN0 = 0xbf, ID_ASSIGN1 = 0x02;    // controller -> here is your channel
constexpr uint8_t ID_ACK0 = 0xbf, ID_ACK1 = 0x03;          // client -> acknowledge
constexpr uint8_t NOTHING0 = 0xbf, NOTHING1 = 0x07;        // client -> nothing to send
constexpr uint8_t CTRL_CFG0 = 0xbf, CTRL_CFG1 = 0x24;   // info (model/version)
constexpr uint8_t CTRL_CFG2_0 = 0xbf, CTRL_CFG2_1 = 0x2e;  // accessory inventory
constexpr uint8_t FILTER0 = 0xbf, FILTER1 = 0x23;
}  // namespace msg

// Bus channels. A client starts UNASSIGNED and MUST be given a channel by the
// controller before it may transmit; it may then only transmit in a Ready
// addressed to that channel. Never hardcode a channel — another client will
// already hold one and the two will collide.
namespace channel {
constexpr uint8_t UNASSIGNED = 0x00;
constexpr uint8_t BROADCAST = 0xfe;  // channel the join handshake runs on
constexpr uint8_t MAX = 0x2f;        // controller assignments are capped here
}  // namespace channel

// Item codes for toggle commands.
namespace item {
constexpr uint8_t NORMAL_OPERATION = 0x01, CLEAR_NOTIFICATION = 0x03;
constexpr uint8_t PUMP1 = 0x04;   // pumpN = PUMP1 + N
constexpr uint8_t BLOWER = 0x0c, MISTER = 0x0e;
constexpr uint8_t LIGHT1 = 0x11;  // lightN = LIGHT1 + N
constexpr uint8_t AUX1 = 0x16;    // auxN   = AUX1 + N
constexpr uint8_t SOAK = 0x1d, HOLD = 0x3c;
constexpr uint8_t TEMPERATURE_RANGE = 0x50, HEATING_MODE = 0x51;
}  // namespace item

bool frame_is(const ParsedFrame &f, uint8_t t0, uint8_t t1);
inline bool is_ready(const ParsedFrame &f) { return frame_is(f, msg::READY0, msg::READY1); }
// A Ready is OURS only when addressed to our assigned channel. Transmitting on
// any other Ready collides with the client that actually owns that window.
inline bool is_ready_for(const ParsedFrame &f, uint8_t id) {
  return id != channel::UNASSIGNED && is_ready(f) && f.src == id;
}
inline bool is_new_client_cts(const ParsedFrame &f) {
  return f.src == channel::BROADCAST && frame_is(f, msg::NEW_CLIENT0, msg::NEW_CLIENT1);
}
inline bool is_channel_assignment(const ParsedFrame &f) {
  return f.src == channel::BROADCAST && frame_is(f, msg::ID_ASSIGN0, msg::ID_ASSIGN1);
}
// Channel carried by an ID_ASSIGN frame, clamped to the controller's valid range.
uint8_t channel_from_assignment(const ParsedFrame &f);

bool decode_status(const ParsedFrame &f, SpaStatus *out);
bool decode_control_config(const ParsedFrame &f, SpaInfo *out);
bool decode_control_config2(const ParsedFrame &f, SpaConfig *out);
bool decode_filter_cycles(const ParsedFrame &f, FilterCyclesData *out);
size_t encode_filter_cycles(uint8_t *out, const FilterCyclesData &fc, uint8_t src);

// `src` is our assigned channel — pass the value the controller gave us.
size_t encode_toggle_item(uint8_t *out, uint8_t item_code, uint8_t src);
size_t encode_set_target_temp(uint8_t *out, uint8_t temp_raw, uint8_t src);
size_t encode_set_time(uint8_t *out, uint8_t hour, uint8_t minute, bool h24, uint8_t src);
size_t encode_set_temp_scale(uint8_t *out, TempScale scale, uint8_t src);
size_t encode_config_request(uint8_t *out, uint8_t src);
size_t encode_control_config_request(uint8_t *out, uint8_t type, uint8_t src);

// Channel negotiation. 0x02 0xf1 0x73 is the client signature used by the
// reference implementation (cribskip/esp8266_spa).
size_t encode_id_request(uint8_t *out);
size_t encode_id_ack(uint8_t *out, uint8_t id);
size_t encode_nothing_to_send(uint8_t *out, uint8_t id);

}  // namespace balboa_spa
}  // namespace esphome
