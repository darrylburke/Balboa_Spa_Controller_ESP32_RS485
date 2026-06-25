#include "messages.h"
#include <cstdio>

namespace esphome {
namespace balboa_spa {

bool frame_is(const ParsedFrame &f, uint8_t t0, uint8_t t1) {
  return f.type0 == t0 && f.type1 == t1;
}

bool decode_status(const ParsedFrame &f, SpaStatus *out) {
  if (!frame_is(f, msg::STATUS0, msg::STATUS1)) return false;
  if (f.payload_len < 21) return false;
  const uint8_t *d = f.payload;

  out->hold = (d[0] & 0x05) != 0;
  out->priming = d[1] == 0x01;
  out->heating_mode = (HeatingMode)(d[5] & 0x03);
  out->notification = (d[1] == 0x03) ? d[6] : 0x00;

  out->temp_scale = (d[9] & 0x01) ? TempScale::CELSIUS : TempScale::FAHRENHEIT;
  out->twenty_four_hour = (d[9] & 0x02) != 0;
  out->filter_running[0] = (d[9] & 0x04) != 0;
  out->filter_running[1] = (d[9] & 0x08) != 0;

  out->heating = (d[10] & 0x30) != 0;
  out->temp_range = (d[10] & 0x04) ? TempRange::HIGH : TempRange::LOW;

  out->pumps[0] = d[11] & 0x03;
  out->pumps[1] = (d[11] >> 2) & 0x03;
  out->pumps[2] = (d[11] >> 4) & 0x03;
  out->pumps[3] = (d[11] >> 6) & 0x03;
  out->pumps[4] = d[12] & 0x03;
  out->pumps[5] = (d[12] >> 2) & 0x03;  // matches reference Status decode

  out->circulation_pump = (d[13] & 0x02) != 0;
  out->blower = (d[13] >> 2) & 0x03;

  out->lights[0] = (d[14] & 0x03) != 0;
  out->lights[1] = ((d[14] >> 2) & 0x03) != 0;

  out->mister = (d[15] & 0x01) != 0;
  out->aux[0] = (d[15] & 0x08) != 0;
  out->aux[1] = (d[15] & 0x10) != 0;

  out->hour = d[3];
  out->minute = d[4];

  out->current_temp_raw = d[2];
  out->current_temp_valid = d[2] != 0xff;
  out->target_temp_raw = d[20];
  out->valid = true;
  return true;
}

bool decode_control_config(const ParsedFrame &f, SpaInfo *out) {
  if (!frame_is(f, msg::CTRL_CFG0, msg::CTRL_CFG1)) return false;
  if (f.payload_len < 12) return false;
  const uint8_t *d = f.payload;
  // version = "V{d[2]}.{d[3]}"
  snprintf(out->version, sizeof(out->version), "V%u.%u", d[2], d[3]);
  // model = ASCII bytes [4..11], trim trailing spaces
  char raw[9];
  for (int i = 0; i < 8; i++) raw[i] = (char)d[4 + i];
  raw[8] = '\0';
  int end = 8;
  while (end > 0 && (raw[end - 1] == ' ' || raw[end - 1] == '\0')) end--;
  for (int i = 0; i < end; i++) out->model[i] = raw[i];
  out->model[end] = '\0';
  out->valid = true;
  return true;
}

bool decode_control_config2(const ParsedFrame &f, SpaConfig *out) {
  if (!frame_is(f, msg::CTRL_CFG2_0, msg::CTRL_CFG2_1)) return false;
  if (f.payload_len < 5) return false;
  const uint8_t *d = f.payload;
  out->pumps[0] = d[0] & 0x03;
  out->pumps[1] = (d[0] >> 2) & 0x03;
  out->pumps[2] = (d[0] >> 4) & 0x03;
  out->pumps[3] = (d[0] >> 6) & 0x03;
  out->pumps[4] = d[1] & 0x03;
  out->pumps[5] = (d[1] >> 6) & 0x03;  // matches reference ControlConfiguration2 decode
  out->lights[0] = (d[2] & 0x03) != 0;
  out->lights[1] = ((d[2] >> 6) & 0x03) != 0;
  out->blower = d[3] & 0x03;
  out->circulation_pump = ((d[3] >> 6) & 0x03) != 0;
  out->mister = (d[4] & 0x30) != 0;
  out->aux[0] = (d[4] & 0x01) != 0;
  out->aux[1] = (d[4] & 0x02) != 0;
  out->valid = true;
  return true;
}

}  // namespace balboa_spa
}  // namespace esphome
