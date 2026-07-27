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

// The published unit must NOT depend on the spa's scale setting. Entities carry a
// static unit_of_measurement, so publishing native values meant a spa switched to
// Celsius emitted Celsius numbers still labelled °F.
TEST(temp_celsius_is_scale_independent) {
  // Same physical temperature, expressed in each of the spa's two encodings.
  float from_f = spa_raw_to_celsius(100, TempScale::FAHRENHEIT);   // 100 F
  float from_c = spa_raw_to_celsius(76, TempScale::CELSIUS);       // 38.0 C
  CHECK(near(from_f, 37.78f));
  CHECK(near(from_c, 38.0f));
  // ...whereas the native helper's unit follows the scale, which is why it must
  // not back an entity with a fixed unit.
  CHECK(near(spa_raw_to_native(100, TempScale::FAHRENHEIT), 100.0f));
  CHECK(near(spa_raw_to_native(76, TempScale::CELSIUS), 38.0f));
}

// Round-tripping through Celsius must land back on the same raw byte, in both
// scales — this is the path an MQTT target_temperature command takes.
TEST(temp_celsius_round_trips_to_raw_in_both_scales) {
  for (uint8_t raw = 60; raw <= 104; raw++) {
    float c = spa_raw_to_celsius(raw, TempScale::FAHRENHEIT);
    CHECK_EQ(celsius_to_spa_raw(c, TempScale::FAHRENHEIT), raw);
  }
  for (uint8_t raw = 40; raw <= 80; raw++) {
    float c = spa_raw_to_celsius(raw, TempScale::CELSIUS);
    CHECK_EQ(celsius_to_spa_raw(c, TempScale::CELSIUS), raw);
  }
}
