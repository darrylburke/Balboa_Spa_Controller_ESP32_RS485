#pragma once
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"
#include "../balboa_spa.h"

namespace esphome {
namespace balboa_spa {

enum SpaBinaryType { HEATING, PRIMING, CIRCULATION_PUMP, FILTER1_RUNNING, FILTER2_RUNNING, BUS_CONNECTED };

class BalboaBinarySensor : public binary_sensor::BinarySensor, public Component {
 public:
  void set_parent(BalboaSpa *p) { parent_ = p; }
  void set_binary_type(SpaBinaryType t) { type_ = t; }
  void setup() override;

 protected:
  void update_from_spa_();
  BalboaSpa *parent_{nullptr};
  SpaBinaryType type_{HEATING};
};

}  // namespace balboa_spa
}  // namespace esphome
