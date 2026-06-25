#include "test_framework.h"

TEST(smoke_hexb_parses_bytes) {
  auto b = hexb("7e 05 0a");
  CHECK_EQ(b.size(), 3);
  CHECK_EQ(b[0], 0x7e);
  CHECK_EQ(b[2], 0x0a);
}
