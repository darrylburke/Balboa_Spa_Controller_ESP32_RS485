#pragma once
#include "esphome/components/number/number.h"
#include "esphome/core/component.h"
#include "../balboa_spa.h"

namespace esphome {
namespace balboa_spa {

enum SpaNumberType {
  C1_START_HOUR, C1_START_MINUTE, C1_DURATION,
  C2_START_HOUR, C2_START_MINUTE, C2_DURATION
};

class BalboaNumber : public number::Number, public Component {
 public:
  void set_parent(BalboaSpa *p) { parent_ = p; }
  void set_number_type(SpaNumberType t) { type_ = t; }
  void setup() override;

 protected:
  void control(float value) override;
  void update_from_spa_();
  BalboaSpa *parent_{nullptr};
  SpaNumberType type_{C1_START_HOUR};
};

}  // namespace balboa_spa
}  // namespace esphome
