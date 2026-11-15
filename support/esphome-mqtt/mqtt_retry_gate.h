#pragma once
#include <cstdint>

namespace esphome::mqtt {

// Presentation retries only. Incoming commands and RS485 writes do not use it.
class MqttRetryGate {
 public:
  static constexpr uint32_t RETRY_MS = 1000;
  bool ready(uint32_t now) const { return !waiting_ || uint32_t(now - last_attempt_) >= RETRY_MS; }
  void failed(uint32_t now) { last_attempt_ = now; waiting_ = true; }
  void succeeded() { waiting_ = false; }
 private:
  uint32_t last_attempt_{0};
  bool waiting_{false};
};

}  // namespace esphome::mqtt
