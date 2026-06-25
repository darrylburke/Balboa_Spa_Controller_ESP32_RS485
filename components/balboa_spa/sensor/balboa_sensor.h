#pragma once
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "../balboa_spa.h"

namespace esphome {
namespace balboa_spa {

enum SpaSensorType { CURRENT_TEMPERATURE, TARGET_TEMPERATURE };

class BalboaSensor : public sensor::Sensor, public Component {
 public:
  void set_parent(BalboaSpa *p) { parent_ = p; }
  void set_sensor_type(SpaSensorType t) { type_ = t; }
  void setup() override;

 protected:
  void update_from_spa_();
  BalboaSpa *parent_{nullptr};
  SpaSensorType type_{CURRENT_TEMPERATURE};
};

}  // namespace balboa_spa
}  // namespace esphome
