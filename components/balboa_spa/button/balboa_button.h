#pragma once
#include "esphome/components/button/button.h"
#include "esphome/core/component.h"
#include "../balboa_spa.h"

namespace esphome {
namespace balboa_spa {

enum SpaButtonType { BTN_NORMAL_OPERATION, BTN_CLEAR_NOTIFICATION, BTN_SOAK };

class BalboaButton : public button::Button, public Component {
 public:
  void set_parent(BalboaSpa *p) { parent_ = p; }
  void set_button_type(SpaButtonType t) { type_ = t; }
  void setup() override {}

 protected:
  void press_action() override;
  BalboaSpa *parent_{nullptr};
  SpaButtonType type_{BTN_NORMAL_OPERATION};
};

}  // namespace balboa_spa
}  // namespace esphome
