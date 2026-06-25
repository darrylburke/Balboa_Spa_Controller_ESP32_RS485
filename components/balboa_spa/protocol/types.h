#pragma once
#include <cstdint>

namespace esphome {
namespace balboa_spa {

enum class TempScale : uint8_t { FAHRENHEIT = 0, CELSIUS = 1 };
enum class HeatingMode : uint8_t { READY = 0, REST = 1, READY_IN_REST = 2 };
enum class TempRange : uint8_t { LOW = 0, HIGH = 1 };

struct SpaStatus {
  bool valid = false;
  bool hold = false, priming = false, heating = false;
  HeatingMode heating_mode = HeatingMode::READY;
  TempScale temp_scale = TempScale::FAHRENHEIT;
  bool twenty_four_hour = false;
  TempRange temp_range = TempRange::HIGH;
  uint8_t hour = 0, minute = 0;
  bool circulation_pump = false;
  uint8_t blower = 0;
  uint8_t pumps[6] = {0, 0, 0, 0, 0, 0};
  bool lights[2] = {false, false};
  bool aux[2] = {false, false};
  bool mister = false;
  bool filter_running[2] = {false, false};
  bool current_temp_valid = false;
  uint8_t current_temp_raw = 0;
  uint8_t target_temp_raw = 0;
  uint8_t notification = 0;
};

struct SpaInfo {
  bool valid = false;
  char model[9] = {0};
  char version[8] = {0};
};

struct SpaConfig {
  bool valid = false;
  uint8_t pumps[6] = {0, 0, 0, 0, 0, 0};
  bool lights[2] = {false, false};
  bool aux[2] = {false, false};
  uint8_t blower = 0;
  bool mister = false;
  bool circulation_pump = false;
};

struct FilterCyclesData {
  bool valid = false;
  uint8_t c1_start_hour = 0, c1_start_minute = 0;
  uint16_t c1_duration_min = 0;
  bool c2_enabled = false;
  uint8_t c2_start_hour = 0, c2_start_minute = 0;
  uint16_t c2_duration_min = 0;
};

}  // namespace balboa_spa
}  // namespace esphome
