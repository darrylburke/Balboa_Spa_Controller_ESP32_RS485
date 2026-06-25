#pragma once
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "../balboa_spa.h"

namespace esphome {
namespace balboa_spa {

enum SpaTextType { MODEL, VERSION, NOTIFICATION };

class BalboaTextSensor : public text_sensor::TextSensor, public Component {
 public:
  void set_parent(BalboaSpa *p) { parent_ = p; }
  void set_text_type(SpaTextType t) { type_ = t; }
  void setup() override;

 protected:
  void update_();
  BalboaSpa *parent_{nullptr};
  SpaTextType type_{MODEL};
  std::string last_;
};

}  // namespace balboa_spa
}  // namespace esphome
