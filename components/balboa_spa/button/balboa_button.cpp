#include "balboa_button.h"
#include "protocol/messages.h"

namespace esphome {
namespace balboa_spa {

void BalboaButton::press_action() {
  uint8_t code;
  switch (type_) {
    case BTN_NORMAL_OPERATION: code = item::NORMAL_OPERATION; break;
    case BTN_CLEAR_NOTIFICATION: code = item::CLEAR_NOTIFICATION; break;
    case BTN_SOAK: code = item::SOAK; break;
    default: return;
  }
  parent_->engine().toggle_item(code);
}

}  // namespace balboa_spa
}  // namespace esphome
