#pragma once
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "../balboa_spa.h"

namespace esphome {
namespace balboa_spa {

enum SpaSwitchType { SW_LIGHT, SW_AUX, SW_MISTER, SW_HOLD, SW_PUMP_ONOFF, SW_BLOWER_ONOFF };

class BalboaSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(BalboaSpa *p) { parent_ = p; }
  void set_switch_type(SpaSwitchType t) { type_ = t; }
  void set_index(uint8_t i) { index_ = i; }
  void setup() override;

 protected:
  void update_from_spa_();
  void write_state(bool state) override;
  bool current_spa_state_();
  BalboaSpa *parent_{nullptr};
  SpaSwitchType type_{SW_LIGHT};
  uint8_t index_{0};
};

}  // namespace balboa_spa
}  // namespace esphome
