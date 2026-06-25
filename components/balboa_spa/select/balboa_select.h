#pragma once
#include "esphome/components/select/select.h"
#include "esphome/core/component.h"
#include "../balboa_spa.h"

namespace esphome {
namespace balboa_spa {

enum SpaSelectType { SEL_HEATING_MODE, SEL_TEMP_RANGE, SEL_TEMP_SCALE };

class BalboaSelect : public select::Select, public Component {
 public:
  void set_parent(BalboaSpa *p) { parent_ = p; }
  void set_select_type(SpaSelectType t) { type_ = t; }
  void setup() override;

 protected:
  void control(const std::string &value) override;
  void update_from_spa_();
  BalboaSpa *parent_{nullptr};
  SpaSelectType type_{SEL_HEATING_MODE};
};

}  // namespace balboa_spa
}  // namespace esphome
