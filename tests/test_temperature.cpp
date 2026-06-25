#include "test_framework.h"
#include "temperature.h"
#include <cmath>

using namespace esphome::balboa_spa;

static bool near(float a, float b) { return std::fabs(a - b) < 0.05f; }

TEST(temp_fahrenheit_to_celsius) {
  CHECK(near(spa_raw_to_celsius(100, TempScale::FAHRENHEIT), 37.78f));
  CHECK(near(spa_raw_to_native(100, TempScale::FAHRENHEIT), 100.0f));
}

TEST(temp_celsius_halfdegree) {
  // raw 80 in Celsius mode == 40.0 C
  CHECK(near(spa_raw_to_celsius(80, TempScale::CELSIUS), 40.0f));
  CHECK(near(spa_raw_to_native(80, TempScale::CELSIUS), 40.0f));
}

TEST(temp_celsius_to_raw) {
  CHECK_EQ(celsius_to_spa_raw(40.0f, TempScale::CELSIUS), 80);    // doubled
  CHECK_EQ(celsius_to_spa_raw(37.78f, TempScale::FAHRENHEIT), 100);
}
