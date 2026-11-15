#include "test_framework.h"
#include "boot_trace.h"
#include <type_traits>
using namespace esphome::balboa_spa;
static_assert(std::is_trivial<BootTrace>::value, "RTC storage must not have a boot-time constructor");

TEST(boot_trace_preserves_watchdog_location_across_later_ota_software_restart) {
  BootTrace trace{};
  trace.begin(1, false); CHECK_EQ(trace.boot_number, 1); CHECK_EQ(trace.fault_reason, 0);
  trace.mark(ExecutionStage::TX_DRAIN); trace.frame_type = 0xbf01; trace.length = 10;
  trace.begin(6, true);
  CHECK_EQ(trace.fault_reason, 6); CHECK_EQ(trace.fault_stage, uint32_t(ExecutionStage::TX_DRAIN));
  CHECK_EQ(trace.fault_boot, 1); CHECK_EQ(trace.fault_frame_type, 0xbf01); CHECK_EQ(trace.fault_length, 10);
  trace.mark(ExecutionStage::OUTSIDE);
  trace.begin(3, false); // OTA recovery must not erase the fault breadcrumb
  CHECK_EQ(trace.fault_reason, 6); CHECK_EQ(trace.fault_stage, uint32_t(ExecutionStage::TX_DRAIN));
  CHECK_EQ(trace.boot_number, 3);
}

TEST(boot_trace_distinguishes_outside_component_and_reinitializes_invalid_rtc) {
  BootTrace trace{}; trace.begin(1, false);
  trace.mark(ExecutionStage::OUTSIDE); trace.begin(6, true);
  CHECK_EQ(trace.fault_reason, 6); CHECK_EQ(trace.fault_stage, 0);
  trace.magic = 0; trace.begin(6, true);
  CHECK_EQ(trace.fault_reason, 0); CHECK_EQ(trace.fault_boot, 0); CHECK_EQ(trace.boot_number, 1);
}

TEST(boot_trace_nested_stages_restore_the_call_site) {
  BootTrace trace{}; trace.begin(1, false); trace.mark(ExecutionStage::PROTOCOL_FEED);
  {
    ExecutionScope outer(trace, ExecutionStage::TX_CHECK);
    CHECK_EQ(trace.stage, uint32_t(ExecutionStage::TX_CHECK));
    { ExecutionScope inner(trace, ExecutionStage::TX_DRAIN); CHECK_EQ(trace.stage, uint32_t(ExecutionStage::TX_DRAIN)); }
    CHECK_EQ(trace.stage, uint32_t(ExecutionStage::TX_CHECK));
  }
  CHECK_EQ(trace.stage, uint32_t(ExecutionStage::PROTOCOL_FEED));
}
