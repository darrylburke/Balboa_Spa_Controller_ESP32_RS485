#pragma once
#include "esphome/components/fan/fan.h"
#include "esphome/core/component.h"
#include "../balboa_spa.h"

namespace esphome {
namespace balboa_spa {

enum SpaFanType { FAN_PUMP, FAN_BLOWER };

class BalboaFan : public fan::Fan, public Component {
 public:
  void set_parent(BalboaSpa *p) { parent_ = p; }
  void set_fan_type(SpaFanType t) { type_ = t; }
  void set_index(uint8_t i) { index_ = i; }
  void set_speed_count(uint8_t n) { speed_count_ = n; }
  void setup() override;
  fan::FanTraits get_traits() override;

 protected:
  void control(const fan::FanCall &call) override;
  void update_from_spa_();
  uint8_t spa_speed_();
  BalboaSpa *parent_{nullptr};
  SpaFanType type_{FAN_PUMP};
  uint8_t index_{0};
  uint8_t speed_count_{2};
};

}  // namespace balboa_spa
}  // namespace esphome
