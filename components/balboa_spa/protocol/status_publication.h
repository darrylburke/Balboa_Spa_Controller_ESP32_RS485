#pragma once
#include <cstdint>
#include "types.h"

namespace esphome {
namespace balboa_spa {

// Presentation policy only; never used by negotiation, queues or UART timing.
class StatusPublication {
 public:
  static constexpr uint32_t ENTITY_REFRESH_MS = 30000;

  bool observe(const SpaStatus &s, uint64_t now) {
    if (!s.valid) return false;
    have_observation_ = true;
    observation_seq_++;
    observed_ms_ = now;
    if (!have_notification_ || !equal_(s, notified_) || now - notified_ms_ >= ENTITY_REFRESH_MS) {
      notified_ = s;
      notified_ms_ = now;
      have_notification_ = true;
      return true;  // changed state has no debounce or periodic wait
    }
    return false;
  }
  bool have_observation() const { return have_observation_; }
  uint64_t observed_ms() const { return observed_ms_; }
  uint32_t observation_seq() const { return observation_seq_; }

 protected:
  // Compare fields rather than struct padding; future fields must be added here.
  static bool equal_(const SpaStatus &a, const SpaStatus &b) {
    if (a.valid != b.valid || a.hold != b.hold || a.priming != b.priming ||
        a.heating != b.heating || a.heating_mode != b.heating_mode ||
        a.temp_scale != b.temp_scale || a.twenty_four_hour != b.twenty_four_hour ||
        a.temp_range != b.temp_range || a.hour != b.hour || a.minute != b.minute ||
        a.circulation_pump != b.circulation_pump || a.blower != b.blower ||
        a.mister != b.mister || a.current_temp_valid != b.current_temp_valid ||
        a.current_temp_raw != b.current_temp_raw || a.target_temp_raw != b.target_temp_raw ||
        a.notification != b.notification) return false;
    for (unsigned i = 0; i < 6; i++) if (a.pumps[i] != b.pumps[i]) return false;
    for (unsigned i = 0; i < 2; i++)
      if (a.lights[i] != b.lights[i] || a.aux[i] != b.aux[i] ||
          a.filter_running[i] != b.filter_running[i]) return false;
    return true;
  }
  SpaStatus notified_{};
  bool have_notification_ = false, have_observation_ = false;
  uint64_t notified_ms_ = 0, observed_ms_ = 0;
  uint32_t observation_seq_ = 0;
};

}  // namespace balboa_spa
}  // namespace esphome
