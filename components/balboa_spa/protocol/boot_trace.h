#pragma once
#include <cstdint>

namespace esphome {
namespace balboa_spa {

enum class ExecutionStage : uint32_t {
  OUTSIDE = 0, SETUP = 1, UART_AVAILABLE = 2, UART_READ = 3, PROTOCOL_FEED = 4,
  STATUS_CALLBACK = 10, ENTITY_CALLBACKS = 11, TIME_SYNC = 12,
  TX_CHECK = 20, TX_WRITE = 21, TX_DRAIN = 22, TX_RELEASED = 23,
  MQTT_PUBLISH = 30
};

// Plain storage: RTC_NOINIT must not run a constructor on each restart. This is
// diagnostic memory only, never registration identity or authority.
struct BootTrace {
  uint32_t magic;
  uint32_t boot_number;
  volatile uint32_t stage;
  volatile uint32_t frame_type;
  volatile uint32_t length;
  uint32_t fault_boot;
  uint32_t fault_reason;
  uint32_t fault_stage;
  uint32_t fault_frame_type;
  uint32_t fault_length;
  static constexpr uint32_t MAGIC = 0x42545231;

  void begin(uint32_t reset_reason, bool fault_reset) {
    if (magic != MAGIC) {
      *this = {};
      magic = MAGIC;
    } else if (fault_reset) {
      fault_boot = boot_number;
      fault_reason = reset_reason;
      fault_stage = stage;
      fault_frame_type = frame_type;
      fault_length = length;
    }
    boot_number++;
    stage = uint32_t(ExecutionStage::SETUP);
    frame_type = length = 0;
  }
  void mark(ExecutionStage value) { stage = uint32_t(value); }
};

class ExecutionScope {
 public:
  ExecutionScope(BootTrace &trace, ExecutionStage stage) : trace_(trace), previous_(trace.stage) { trace.mark(stage); }
  ~ExecutionScope() { trace_.stage = previous_; }
 private:
  BootTrace &trace_;
  uint32_t previous_;
};

}  // namespace balboa_spa
}  // namespace esphome
