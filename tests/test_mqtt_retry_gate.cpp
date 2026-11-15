#include "test_framework.h"
#include "../support/esphome-mqtt/mqtt_retry_gate.h"
using esphome::mqtt::MqttRetryGate;

TEST(mqtt_pending_state_retry_cannot_spin_on_a_full_outbound_queue) {
  MqttRetryGate gate; unsigned attempts = 0;
  for (uint32_t ms = 0; ms <= 5000; ms++)
    if (gate.ready(ms)) { attempts++; gate.failed(ms); }
  CHECK_EQ(attempts, 6);
}

TEST(mqtt_delayed_state_retry_reads_latest_state_instead_of_a_fifo_copy) {
  MqttRetryGate gate; gate.failed(0);
  unsigned latest = 0, sent = 0;
  bool suppressed = true;
  for (uint32_t ms = 1; ms <= 1000; ms++) {
    latest = ms;
    if (gate.ready(ms)) { sent = latest; gate.succeeded(); }
    if (ms < 1000) suppressed &= sent == 0;
  }
  CHECK(suppressed); CHECK_EQ(sent, 1000); CHECK(gate.ready(1001));
}

TEST(mqtt_retry_gate_handles_uptime_wrap_and_success_removes_delay) {
  MqttRetryGate gate; gate.failed(0xffffff00U);
  CHECK(!gate.ready(0xffffff01U)); CHECK(!gate.ready(0x000002e7U));
  CHECK(gate.ready(0x000002e8U));
  gate.succeeded(); CHECK(gate.ready(0x000002e9U));
  gate.failed(1000); CHECK(gate.ready(0x90000000U)); // long offline periods cannot create a multi-day wait
}
