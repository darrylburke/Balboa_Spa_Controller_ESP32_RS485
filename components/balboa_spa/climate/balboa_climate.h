#pragma once
#include "esphome/components/climate/climate.h"
#include "esphome/core/component.h"
#include "../balboa_spa.h"

namespace esphome {
namespace balboa_spa {

class BalboaClimate : public climate::Climate, public Component {
 public:
  void set_parent(BalboaSpa *parent) { parent_ = parent; }
  void setup() override;
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

 protected:
  void update_from_spa_();
  BalboaSpa *parent_{nullptr};
};

}  // namespace balboa_spa
}  // namespace esphome
