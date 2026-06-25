#include "test_framework.h"
#include "crc.h"

using namespace esphome::balboa_spa;

TEST(crc_config_request_body) {
  // body of the config-request frame: LEN SRC TYPE0 TYPE1 = 05 0a bf 04
  auto body = hexb("05 0a bf 04");
  CHECK_EQ(balboa_crc8(body.data(), body.size()), 0x77);
}

TEST(crc_empty_is_init_xor_xorout) {
  CHECK_EQ(balboa_crc8(nullptr, 0), 0x00);  // 0x02 ^ 0x02
}

TEST(crc_single_zero_byte) {
  uint8_t z = 0x00;
  CHECK_EQ(balboa_crc8(&z, 1), 0x0c);
}
