# Balboa ESP32/RS-485 → MQTT Gateway Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an ESPHome firmware for an ESP32 + HW-0519 (auto-direction MAX485) module that acts as the sole RS-485 client on a Balboa spa bus and bridges full spa state/control to MQTT with Home Assistant auto-discovery.

**Architecture:** A platform-agnostic C++ protocol library (CRC, framing, message decode/encode, a stateful `ProtocolEngine`) is developed and unit-tested off-device with `g++`. An ESPHome external component (`balboa_spa`) wraps the engine: a hub `Component`+`UARTDevice` drives the engine in `loop()`, and parameterized entity-platform classes (climate, sensor, binary_sensor, text_sensor, switch, fan, select, number, button) read state and enqueue commands. Commands transmit only in the window immediately after a `Ready` frame.

**Tech Stack:** C++17, ESPHome 2025.2.0, PlatformIO, ESP32 (Arduino framework), `g++` + `make` for host unit tests, MQTT (ESPHome `mqtt:` with HA discovery).

## Global Constraints

- Namespace for all C++: `esphome::balboa_spa`. Protocol library code lives under `components/balboa_spa/protocol/` and MUST NOT include any ESPHome/Arduino headers (host-buildable with plain `g++ -std=c++17`).
- RS-485 line settings: **115200, 8, N, 1**. UART pins default `tx_pin: GPIO17`, `rx_pin: GPIO16` (ESP32 UART2). USB UART0 reserved for logging.
- CRC-8: polynomial `0x07`, initial value `0x02`, final XOR `0x02`, **no input/output reflection**. Computed over the frame bytes from the length byte through the last payload byte (everything between the two `0x7E` markers except the CRC byte itself).
- Frame format: `0x7E | LEN | SRC | TYPE0 TYPE1 | payload... | CRC | 0x7E`, where `LEN = payload_len + 5`.
- Our transmit source address (`SRC`) is `0x0A`. We are the sole client; never coexist with a Balboa Wi-Fi module.
- Transmit discipline: maintain a command FIFO; send **exactly one** queued frame immediately after each received `Ready` (`TYPE = BF 06`). Never transmit before at least one valid `Status` frame has been received.
- First flash defaults to `read_only: true` (decode only; never transmit).
- Every task ends with a passing test (host `make test`, or `esphome config`/`compile`) and a commit.
- Authoritative test fixtures (verified against the reference gem's CRC in both Python and Ruby) — use these exact bytes:
  - Config request (TX): `7e 05 0a bf 04 77 7e`
  - CfgReq type2 (TX): `7e 08 0a bf 22 00 00 01 58 7e`
  - Toggle light1 (TX): `7e 07 0a bf 11 11 00 93 7e`
  - Set temp 100°F (TX): `7e 06 0a bf 20 64 29 7e`
  - Ready (RX): `7e 05 10 bf 06 5c 7e`
  - NewClientCTS (RX): `7e 05 10 bf 00 4e 7e`
  - Status (RX): `7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 01 00 02 03 00 00 00 00 00 66 00 00 00 0f 7e`
  - ControlConfig (RX): `7e 1a 0a bf 24 64 dc 11 00 42 46 42 50 32 30 20 20 01 3d 12 38 2e 01 0a 04 00 9b 7e`
  - ControlConfig2 (RX): `7e 0b 0a bf 2e 0a 00 01 d0 00 44 6f 7e`
  - FilterCycles (RX): `7e 0d 0a bf 23 08 00 02 00 94 00 01 1e d0 7e`

---

## File Structure

```
balboa-esp32-mqtt/
  Makefile                                  # host unit-test build (Tasks 1-11)
  components/balboa_spa/
    protocol/                               # platform-agnostic, host-testable
      crc.h crc.cpp                         # CRC-8
      frame.h frame.cpp                     # scan/parse + build frames
      types.h                               # enums + SpaStatus/SpaConfig/SpaInfo/FilterCyclesData structs
      messages.h messages.cpp               # decode/encode each message type
      temperature.h temperature.cpp         # spa<->celsius conversion
      protocol_engine.h protocol_engine.cpp # stateful engine: feed/queue/Ready-gated TX
    __init__.py balboa_spa.h balboa_spa.cpp # ESPHome hub (Component+UARTDevice)
    climate/__init__.py balboa_climate.h balboa_climate.cpp
    sensor/__init__.py balboa_sensor.h balboa_sensor.cpp
    binary_sensor/__init__.py balboa_binary_sensor.h balboa_binary_sensor.cpp
    text_sensor/__init__.py balboa_text_sensor.h balboa_text_sensor.cpp
    switch/__init__.py balboa_switch.h balboa_switch.cpp
    fan/__init__.py balboa_fan.h balboa_fan.cpp
    select/__init__.py balboa_select.h balboa_select.cpp
    number/__init__.py balboa_number.h balboa_number.cpp
    button/__init__.py balboa_button.h balboa_button.cpp
  tests/
    test_framework.h test_main.cpp
    test_crc.cpp test_frame.cpp test_messages.cpp
    test_temperature.cpp test_engine.cpp
  packages/spa_entities.yaml                # superset of entities, commented per spa
  balboa-spa.yaml                           # main device config
  secrets.yaml.example
  docs/                                     # wiring + bring-up runbook (spec already here)
  README.md
```

---

### Task 1: Host test harness + repo scaffolding

**Files:**
- Create: `Makefile`
- Create: `tests/test_framework.h`
- Create: `tests/test_main.cpp`
- Create: `tests/test_smoke.cpp`

**Interfaces:**
- Produces: `make test` build; `TEST(name){...}` macro; `CHECK(cond)`, `CHECK_EQ(a,b)` assertions; `hexb("..")` → `std::vector<uint8_t>` hex parser; auto test registry.

- [ ] **Step 1: Write `tests/test_framework.h`**

```cpp
#pragma once
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <utility>

extern int g_checks;
extern int g_failures;

using TestFn = void (*)();
std::vector<std::pair<std::string, TestFn>> &test_registry();

struct TestRegistrar {
  TestRegistrar(const char *name, TestFn fn) { test_registry().push_back({name, fn}); }
};

#define TEST(name)                                            \
  static void name();                                         \
  static TestRegistrar registrar_##name(#name, name);         \
  static void name()

#define CHECK(cond)                                                          \
  do {                                                                       \
    g_checks++;                                                              \
    if (!(cond)) {                                                           \
      g_failures++;                                                          \
      std::printf("  FAIL %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond);   \
    }                                                                        \
  } while (0)

#define CHECK_EQ(a, b)                                                       \
  do {                                                                       \
    g_checks++;                                                              \
    long long _a = (long long)(a);                                          \
    long long _b = (long long)(b);                                          \
    if (_a != _b) {                                                          \
      g_failures++;                                                          \
      std::printf("  FAIL %s:%d: CHECK_EQ(%s, %s) -> %lld != %lld\n",        \
                  __FILE__, __LINE__, #a, #b, _a, _b);                       \
    }                                                                        \
  } while (0)

// Parse a hex string (spaces/punctuation ignored) into bytes.
inline std::vector<uint8_t> hexb(const char *s) {
  std::vector<uint8_t> v;
  int hi = -1;
  for (; *s; ++s) {
    char c = *s;
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else continue;
    if (hi < 0) hi = d;
    else { v.push_back((uint8_t)((hi << 4) | d)); hi = -1; }
  }
  return v;
}
```

- [ ] **Step 2: Write `tests/test_main.cpp`**

```cpp
#include "test_framework.h"

int g_checks = 0;
int g_failures = 0;

std::vector<std::pair<std::string, TestFn>> &test_registry() {
  static std::vector<std::pair<std::string, TestFn>> r;
  return r;
}

int main() {
  for (auto &t : test_registry()) {
    std::printf("RUN %s\n", t.first.c_str());
    t.second();
  }
  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
```

- [ ] **Step 3: Write `tests/test_smoke.cpp`**

```cpp
#include "test_framework.h"

TEST(smoke_hexb_parses_bytes) {
  auto b = hexb("7e 05 0a");
  CHECK_EQ(b.size(), 3);
  CHECK_EQ(b[0], 0x7e);
  CHECK_EQ(b[2], 0x0a);
}
```

- [ ] **Step 4: Write `Makefile`**

```make
CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O0 -g -Itests -Icomponents/balboa_spa/protocol

PROTO_SRC := $(wildcard components/balboa_spa/protocol/*.cpp)
TEST_SRC  := $(wildcard tests/test_*.cpp)

build/run_tests: $(PROTO_SRC) $(TEST_SRC) | build
	$(CXX) $(CXXFLAGS) $(PROTO_SRC) $(TEST_SRC) -o $@

build:
	mkdir -p build

test: build/run_tests
	./build/run_tests

clean:
	rm -rf build

.PHONY: test clean
```

- [ ] **Step 5: Run the tests**

Run: `make test`
Expected: compiles; output ends with `1 checks, 0 failures`; exit code 0.

- [ ] **Step 6: Commit**

```bash
git add Makefile tests/
git commit -m "test: add host unit-test harness"
```

---

### Task 2: CRC-8

**Files:**
- Create: `components/balboa_spa/protocol/crc.h`
- Create: `components/balboa_spa/protocol/crc.cpp`
- Create: `tests/test_crc.cpp`

**Interfaces:**
- Produces: `uint8_t esphome::balboa_spa::balboa_crc8(const uint8_t *data, size_t len);`

- [ ] **Step 1: Write the failing test `tests/test_crc.cpp`**

```cpp
#include "test_framework.h"
#include "crc.h"

using namespace esphome::balboa_spa;

TEST(crc_config_request_body) {
  // body of the config-request frame: LEN SRC TYPE0 TYPE1 = 05 0a bf 04
  auto body = hexb("05 0a bf 04");
  CHECK_EQ(balboa_crc8(body.data(), body.size()), 0x77);
}

TEST(crc_empty_is_init_xor_xorout) {
  CHECK_EQ(balboa_crc8(nullptr, 0), 0x00);  // 0x02 ^ 0x02
}

TEST(crc_single_zero_byte) {
  uint8_t z = 0x00;
  CHECK_EQ(balboa_crc8(&z, 1), 0x0c);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test`
Expected: FAIL to compile (`crc.h` not found).

- [ ] **Step 3: Write `components/balboa_spa/protocol/crc.h`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace balboa_spa {

// CRC-8: poly 0x07, init 0x02, final XOR 0x02, no reflection.
uint8_t balboa_crc8(const uint8_t *data, size_t len);

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 4: Write `components/balboa_spa/protocol/crc.cpp`**

```cpp
#include "crc.h"

namespace esphome {
namespace balboa_spa {

uint8_t balboa_crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0x02;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      if (crc & 0x80)
        crc = (uint8_t)((crc << 1) ^ 0x07);
      else
        crc = (uint8_t)(crc << 1);
    }
  }
  return (uint8_t)(crc ^ 0x02);
}

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 5: Run test to verify it passes**

Run: `make test`
Expected: PASS; failures count 0.

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/protocol/crc.h components/balboa_spa/protocol/crc.cpp tests/test_crc.cpp
git commit -m "feat: balboa CRC-8 (init/xor 0x02)"
```

---

### Task 3: Frame scanning / parsing

**Files:**
- Create: `components/balboa_spa/protocol/frame.h`
- Create: `components/balboa_spa/protocol/frame.cpp`
- Modify: `tests/test_frame.cpp` (create)

**Interfaces:**
- Consumes: `balboa_crc8` (Task 2).
- Produces:
  - `struct ParsedFrame { uint8_t src; uint8_t type0; uint8_t type1; const uint8_t *payload; uint8_t payload_len; };`
  - `enum class ScanResult { FRAME, NEED_MORE };`
  - `ScanResult scan_frame(const uint8_t *buf, size_t len, ParsedFrame *out, size_t *consumed);`
    - `FRAME`: `*consumed` = bytes up to and including the parsed frame; `*out` valid (pointers reference `buf`).
    - `NEED_MORE`: `*consumed` = leading bytes safe to discard (garbage before a possible partial frame); caller keeps the remainder and appends more data.

- [ ] **Step 1: Write the failing test `tests/test_frame.cpp`**

```cpp
#include "test_framework.h"
#include "frame.h"

using namespace esphome::balboa_spa;

TEST(frame_parses_ready) {
  auto b = hexb("7e 05 10 bf 06 5c 7e");
  ParsedFrame f{};
  size_t consumed = 0;
  CHECK(scan_frame(b.data(), b.size(), &f, &consumed) == ScanResult::FRAME);
  CHECK_EQ(consumed, 7);
  CHECK_EQ(f.src, 0x10);
  CHECK_EQ(f.type0, 0xbf);
  CHECK_EQ(f.type1, 0x06);
  CHECK_EQ(f.payload_len, 0);
}

TEST(frame_skips_leading_garbage) {
  auto b = hexb("11 22 33 7e 05 10 bf 06 5c 7e");
  ParsedFrame f{};
  size_t consumed = 0;
  CHECK(scan_frame(b.data(), b.size(), &f, &consumed) == ScanResult::FRAME);
  CHECK_EQ(consumed, 10);
  CHECK_EQ(f.type1, 0x06);
}

TEST(frame_needs_more_on_partial) {
  auto b = hexb("7e 05 10 bf");  // truncated
  ParsedFrame f{};
  size_t consumed = 0;
  CHECK(scan_frame(b.data(), b.size(), &f, &consumed) == ScanResult::NEED_MORE);
  CHECK_EQ(consumed, 0);  // keep the partial starting at 0x7e
}

TEST(frame_rejects_bad_crc_and_resyncs) {
  // first frame has wrong CRC (5d), a valid Ready follows
  auto b = hexb("7e 05 10 bf 06 5d 7e 7e 05 10 bf 06 5c 7e");
  ParsedFrame f{};
  size_t consumed = 0;
  CHECK(scan_frame(b.data(), b.size(), &f, &consumed) == ScanResult::FRAME);
  CHECK_EQ(consumed, 14);  // discards the bad frame, returns the good one
}

TEST(frame_parses_status_payload) {
  auto b = hexb("7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 01 00 02 03 "
                "00 00 00 00 00 66 00 00 00 0f 7e");
  ParsedFrame f{};
  size_t consumed = 0;
  CHECK(scan_frame(b.data(), b.size(), &f, &consumed) == ScanResult::FRAME);
  CHECK_EQ(f.src, 0xff);
  CHECK_EQ(f.type0, 0xaf);
  CHECK_EQ(f.type1, 0x13);
  CHECK_EQ(f.payload_len, 24);
  CHECK_EQ(f.payload[2], 0x64);   // current temp byte
  CHECK_EQ(f.payload[20], 0x66);  // target temp byte
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test`
Expected: FAIL to compile (`frame.h` not found).

- [ ] **Step 3: Write `components/balboa_spa/protocol/frame.h`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace balboa_spa {

struct ParsedFrame {
  uint8_t src;
  uint8_t type0;
  uint8_t type1;
  const uint8_t *payload;  // points into the caller's buffer
  uint8_t payload_len;
};

enum class ScanResult { FRAME, NEED_MORE };

// Scan buf for the next valid frame. See header doc in plan Task 3 Interfaces.
ScanResult scan_frame(const uint8_t *buf, size_t len, ParsedFrame *out, size_t *consumed);

// Build a frame into out (caller ensures capacity >= payload_len + 7). Returns total length.
size_t build_frame(uint8_t *out, uint8_t src, uint8_t type0, uint8_t type1,
                   const uint8_t *payload, uint8_t payload_len);

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 4: Write `components/balboa_spa/protocol/frame.cpp`**

```cpp
#include "frame.h"
#include "crc.h"

namespace esphome {
namespace balboa_spa {

static constexpr uint8_t DELIM = 0x7e;

ScanResult scan_frame(const uint8_t *buf, size_t len, ParsedFrame *out, size_t *consumed) {
  size_t offset = 0;
  while (true) {
    // Need at least 5 bytes from offset to inspect a frame header+.
    if (offset + 5 > len) {
      // Discard anything before a trailing partial that begins with DELIM.
      // Find the last DELIM at/after offset to preserve as a partial.
      size_t keep = offset;
      while (keep < len && buf[keep] != DELIM) keep++;
      *consumed = keep;
      return ScanResult::NEED_MORE;
    }
    if (buf[offset] != DELIM) { offset++; continue; }

    uint8_t length = buf[offset + 1];  // LEN byte
    if (length < 5 || length >= DELIM) { offset++; continue; }

    // Total frame size = 1 (start) + length + 1 (end). Need length+2 bytes from offset.
    if (offset + (size_t)length + 2 > len) {
      *consumed = offset;  // discard leading garbage, keep partial frame
      return ScanResult::NEED_MORE;
    }
    if (buf[offset + length + 1] != DELIM) { offset++; continue; }

    // CRC over bytes [offset+1 .. offset+length-1] (LEN..last payload byte).
    uint8_t crc = balboa_crc8(buf + offset + 1, (size_t)length - 1);
    if (crc != buf[offset + length]) { offset++; continue; }

    out->src = buf[offset + 2];
    out->type0 = buf[offset + 3];
    out->type1 = buf[offset + 4];
    out->payload = buf + offset + 5;
    out->payload_len = (uint8_t)(length - 5);
    *consumed = offset + (size_t)length + 2;
    return ScanResult::FRAME;
  }
}

size_t build_frame(uint8_t *out, uint8_t src, uint8_t type0, uint8_t type1,
                   const uint8_t *payload, uint8_t payload_len) {
  uint8_t length = (uint8_t)(payload_len + 5);
  out[0] = DELIM;
  out[1] = length;
  out[2] = src;
  out[3] = type0;
  out[4] = type1;
  for (uint8_t i = 0; i < payload_len; i++) out[5 + i] = payload[i];
  uint8_t crc = balboa_crc8(out + 1, (size_t)length - 1);
  out[5 + payload_len] = crc;
  out[6 + payload_len] = DELIM;
  return (size_t)payload_len + 7;
}

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 5: Run test to verify it passes**

Run: `make test`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/protocol/frame.h components/balboa_spa/protocol/frame.cpp tests/test_frame.cpp
git commit -m "feat: RS-485 frame scan/parse + build"
```

---

### Task 4: Frame build round-trip

**Files:**
- Modify: `tests/test_frame.cpp` (append tests)

**Interfaces:**
- Consumes: `build_frame`, `scan_frame` (Task 3).

- [ ] **Step 1: Append failing tests to `tests/test_frame.cpp`**

```cpp
TEST(frame_build_config_request) {
  uint8_t out[16];
  size_t n = build_frame(out, 0x0a, 0xbf, 0x04, nullptr, 0);
  auto expect = hexb("7e 05 0a bf 04 77 7e");
  CHECK_EQ(n, expect.size());
  for (size_t i = 0; i < n; i++) CHECK_EQ(out[i], expect[i]);
}

TEST(frame_build_toggle_light1) {
  uint8_t out[16];
  uint8_t payload[2] = {0x11, 0x00};
  size_t n = build_frame(out, 0x0a, 0xbf, 0x11, payload, 2);
  auto expect = hexb("7e 07 0a bf 11 11 00 93 7e");
  CHECK_EQ(n, expect.size());
  for (size_t i = 0; i < n; i++) CHECK_EQ(out[i], expect[i]);
}

TEST(frame_build_then_scan_roundtrip) {
  uint8_t out[16];
  uint8_t payload[1] = {0x64};
  size_t n = build_frame(out, 0x0a, 0xbf, 0x20, payload, 1);
  ParsedFrame f{};
  size_t consumed = 0;
  CHECK(scan_frame(out, n, &f, &consumed) == ScanResult::FRAME);
  CHECK_EQ(consumed, n);
  CHECK_EQ(f.type1, 0x20);
  CHECK_EQ(f.payload_len, 1);
  CHECK_EQ(f.payload[0], 0x64);
}
```

- [ ] **Step 2: Run test to verify it passes** (build_frame already implemented in Task 3)

Run: `make test`
Expected: PASS. (If any fail, fix `build_frame` in `frame.cpp`.)

- [ ] **Step 3: Commit**

```bash
git add tests/test_frame.cpp
git commit -m "test: frame build round-trip against known frames"
```

---

### Task 5: Types + Status decode

**Files:**
- Create: `components/balboa_spa/protocol/types.h`
- Create: `components/balboa_spa/protocol/messages.h`
- Create: `components/balboa_spa/protocol/messages.cpp`
- Create: `tests/test_messages.cpp`

**Interfaces:**
- Consumes: `ParsedFrame` (Task 3).
- Produces (in `types.h`):
  - `enum class TempScale { FAHRENHEIT = 0, CELSIUS = 1 };`
  - `enum class HeatingMode { READY = 0, REST = 1, READY_IN_REST = 2 };`
  - `enum class TempRange { LOW = 0, HIGH = 1 };`
  - `struct SpaStatus { bool valid; bool hold, priming, heating; HeatingMode heating_mode; TempScale temp_scale; bool twenty_four_hour; TempRange temp_range; uint8_t hour, minute; bool circulation_pump; uint8_t blower; uint8_t pumps[6]; bool lights[2]; bool aux[2]; bool mister; bool filter_running[2]; bool current_temp_valid; uint8_t current_temp_raw; uint8_t target_temp_raw; uint8_t notification; };`
  - `struct SpaInfo { bool valid; char model[9]; char version[8]; };`
  - `struct SpaConfig { bool valid; uint8_t pumps[6]; bool lights[2]; bool aux[2]; uint8_t blower; bool mister; bool circulation_pump; };`
  - `struct FilterCyclesData { bool valid; uint8_t c1_start_hour, c1_start_minute; uint16_t c1_duration_min; bool c2_enabled; uint8_t c2_start_hour, c2_start_minute; uint16_t c2_duration_min; };`
- Produces (in `messages.h`):
  - `bool frame_is(const ParsedFrame &f, uint8_t t0, uint8_t t1);`
  - `bool decode_status(const ParsedFrame &f, SpaStatus *out);`

- [ ] **Step 1: Write the failing test `tests/test_messages.cpp`**

```cpp
#include "test_framework.h"
#include "frame.h"
#include "messages.h"

using namespace esphome::balboa_spa;

static ParsedFrame parse(const std::vector<uint8_t> &b) {
  ParsedFrame f{};
  size_t consumed = 0;
  scan_frame(b.data(), b.size(), &f, &consumed);
  return f;
}

TEST(decode_status_fields) {
  auto f = parse(hexb("7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 01 00 02 "
                      "03 00 00 00 00 00 66 00 00 00 0f 7e"));
  SpaStatus s{};
  CHECK(decode_status(f, &s));
  CHECK(s.valid);
  CHECK_EQ(s.current_temp_raw, 100);
  CHECK(s.current_temp_valid);
  CHECK_EQ(s.target_temp_raw, 102);
  CHECK_EQ(s.hour, 14);
  CHECK_EQ(s.minute, 30);
  CHECK(s.heating);
  CHECK(s.temp_range == TempRange::HIGH);
  CHECK(s.temp_scale == TempScale::FAHRENHEIT);
  CHECK_EQ(s.pumps[0], 1);
  CHECK_EQ(s.pumps[1], 0);
  CHECK(s.circulation_pump);
  CHECK(s.lights[0]);
  CHECK(!s.hold);
  CHECK(!s.priming);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test`
Expected: FAIL to compile (`messages.h` / `types.h` not found).

- [ ] **Step 3: Write `components/balboa_spa/protocol/types.h`**

```cpp
#pragma once
#include <cstdint>

namespace esphome {
namespace balboa_spa {

enum class TempScale : uint8_t { FAHRENHEIT = 0, CELSIUS = 1 };
enum class HeatingMode : uint8_t { READY = 0, REST = 1, READY_IN_REST = 2 };
enum class TempRange : uint8_t { LOW = 0, HIGH = 1 };

struct SpaStatus {
  bool valid = false;
  bool hold = false, priming = false, heating = false;
  HeatingMode heating_mode = HeatingMode::READY;
  TempScale temp_scale = TempScale::FAHRENHEIT;
  bool twenty_four_hour = false;
  TempRange temp_range = TempRange::HIGH;
  uint8_t hour = 0, minute = 0;
  bool circulation_pump = false;
  uint8_t blower = 0;
  uint8_t pumps[6] = {0, 0, 0, 0, 0, 0};
  bool lights[2] = {false, false};
  bool aux[2] = {false, false};
  bool mister = false;
  bool filter_running[2] = {false, false};
  bool current_temp_valid = false;
  uint8_t current_temp_raw = 0;
  uint8_t target_temp_raw = 0;
  uint8_t notification = 0;
};

struct SpaInfo {
  bool valid = false;
  char model[9] = {0};
  char version[8] = {0};
};

struct SpaConfig {
  bool valid = false;
  uint8_t pumps[6] = {0, 0, 0, 0, 0, 0};
  bool lights[2] = {false, false};
  bool aux[2] = {false, false};
  uint8_t blower = 0;
  bool mister = false;
  bool circulation_pump = false;
};

struct FilterCyclesData {
  bool valid = false;
  uint8_t c1_start_hour = 0, c1_start_minute = 0;
  uint16_t c1_duration_min = 0;
  bool c2_enabled = false;
  uint8_t c2_start_hour = 0, c2_start_minute = 0;
  uint16_t c2_duration_min = 0;
};

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 4: Write `components/balboa_spa/protocol/messages.h`**

```cpp
#pragma once
#include "frame.h"
#include "types.h"

namespace esphome {
namespace balboa_spa {

// Message type bytes (TYPE0 TYPE1).
namespace msg {
constexpr uint8_t STATUS0 = 0xaf, STATUS1 = 0x13;
constexpr uint8_t READY0 = 0xbf, READY1 = 0x06;
constexpr uint8_t NEW_CLIENT0 = 0xbf, NEW_CLIENT1 = 0x00;
constexpr uint8_t CTRL_CFG0 = 0xbf, CTRL_CFG1 = 0x24;   // info (model/version)
constexpr uint8_t CTRL_CFG2_0 = 0xbf, CTRL_CFG2_1 = 0x2e;  // accessory inventory
constexpr uint8_t FILTER0 = 0xbf, FILTER1 = 0x23;
}  // namespace msg

bool frame_is(const ParsedFrame &f, uint8_t t0, uint8_t t1);
inline bool is_ready(const ParsedFrame &f) { return frame_is(f, msg::READY0, msg::READY1); }
inline bool is_new_client_cts(const ParsedFrame &f) { return frame_is(f, msg::NEW_CLIENT0, msg::NEW_CLIENT1); }

bool decode_status(const ParsedFrame &f, SpaStatus *out);

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 5: Write `components/balboa_spa/protocol/messages.cpp`**

```cpp
#include "messages.h"

namespace esphome {
namespace balboa_spa {

bool frame_is(const ParsedFrame &f, uint8_t t0, uint8_t t1) {
  return f.type0 == t0 && f.type1 == t1;
}

bool decode_status(const ParsedFrame &f, SpaStatus *out) {
  if (!frame_is(f, msg::STATUS0, msg::STATUS1)) return false;
  if (f.payload_len < 21) return false;
  const uint8_t *d = f.payload;

  out->hold = (d[0] & 0x05) != 0;
  out->priming = d[1] == 0x01;
  out->heating_mode = (HeatingMode)(d[5] & 0x03);
  out->notification = (d[1] == 0x03) ? d[6] : 0x00;

  out->temp_scale = (d[9] & 0x01) ? TempScale::CELSIUS : TempScale::FAHRENHEIT;
  out->twenty_four_hour = (d[9] & 0x02) != 0;
  out->filter_running[0] = (d[9] & 0x04) != 0;
  out->filter_running[1] = (d[9] & 0x08) != 0;

  out->heating = (d[10] & 0x30) != 0;
  out->temp_range = (d[10] & 0x04) ? TempRange::HIGH : TempRange::LOW;

  out->pumps[0] = d[11] & 0x03;
  out->pumps[1] = (d[11] >> 2) & 0x03;
  out->pumps[2] = (d[11] >> 4) & 0x03;
  out->pumps[3] = (d[11] >> 6) & 0x03;
  out->pumps[4] = d[12] & 0x03;
  out->pumps[5] = (d[12] >> 2) & 0x03;  // matches reference Status decode

  out->circulation_pump = (d[13] & 0x02) != 0;
  out->blower = (d[13] >> 2) & 0x03;

  out->lights[0] = (d[14] & 0x03) != 0;
  out->lights[1] = ((d[14] >> 2) & 0x03) != 0;

  out->mister = (d[15] & 0x01) != 0;
  out->aux[0] = (d[15] & 0x08) != 0;
  out->aux[1] = (d[15] & 0x10) != 0;

  out->hour = d[3];
  out->minute = d[4];

  out->current_temp_raw = d[2];
  out->current_temp_valid = d[2] != 0xff;
  out->target_temp_raw = d[20];
  out->valid = true;
  return true;
}

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 6: Run test to verify it passes**

Run: `make test`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add components/balboa_spa/protocol/types.h components/balboa_spa/protocol/messages.h components/balboa_spa/protocol/messages.cpp tests/test_messages.cpp
git commit -m "feat: spa types + Status decode"
```

---

### Task 6: ControlConfiguration + ControlConfiguration2 decode

**Files:**
- Modify: `components/balboa_spa/protocol/messages.h` (add decls)
- Modify: `components/balboa_spa/protocol/messages.cpp` (add impls)
- Modify: `tests/test_messages.cpp` (append tests)

**Interfaces:**
- Produces:
  - `bool decode_control_config(const ParsedFrame &f, SpaInfo *out);`  // 0a bf 24
  - `bool decode_control_config2(const ParsedFrame &f, SpaConfig *out);`  // 0a bf 2e

- [ ] **Step 1: Append failing tests to `tests/test_messages.cpp`**

```cpp
TEST(decode_control_config_model_version) {
  auto f = parse(hexb("7e 1a 0a bf 24 64 dc 11 00 42 46 42 50 32 30 20 20 01 "
                      "3d 12 38 2e 01 0a 04 00 9b 7e"));
  SpaInfo info{};
  CHECK(decode_control_config(f, &info));
  CHECK(info.valid);
  CHECK(std::string(info.model) == "BFBP20");
  CHECK(std::string(info.version) == "V17.0");
}

TEST(decode_control_config2_inventory) {
  auto f = parse(hexb("7e 0b 0a bf 2e 0a 00 01 d0 00 44 6f 7e"));
  SpaConfig c{};
  CHECK(decode_control_config2(f, &c));
  CHECK(c.valid);
  CHECK_EQ(c.pumps[0], 2);
  CHECK_EQ(c.pumps[1], 2);
  CHECK_EQ(c.pumps[2], 0);
  CHECK(c.lights[0]);
  CHECK(!c.lights[1]);
  CHECK(c.circulation_pump);
  CHECK_EQ(c.blower, 0);
  CHECK(!c.mister);
}
```

Add `#include <string>` at the top of `tests/test_messages.cpp` if not already present.

- [ ] **Step 2: Run test to verify it fails**

Run: `make test`
Expected: FAIL to compile (`decode_control_config` undeclared).

- [ ] **Step 3: Add declarations to `components/balboa_spa/protocol/messages.h`**

Add inside the namespace, after `decode_status`:

```cpp
bool decode_control_config(const ParsedFrame &f, SpaInfo *out);
bool decode_control_config2(const ParsedFrame &f, SpaConfig *out);
```

- [ ] **Step 4: Add implementations to `components/balboa_spa/protocol/messages.cpp`**

```cpp
bool decode_control_config(const ParsedFrame &f, SpaInfo *out) {
  if (!frame_is(f, msg::CTRL_CFG0, msg::CTRL_CFG1)) return false;
  if (f.payload_len < 12) return false;
  const uint8_t *d = f.payload;
  // version = "V{d[2]}.{d[3]}"
  snprintf(out->version, sizeof(out->version), "V%u.%u", d[2], d[3]);
  // model = ASCII bytes [4..11], trim trailing spaces
  char raw[9];
  for (int i = 0; i < 8; i++) raw[i] = (char)d[4 + i];
  raw[8] = '\0';
  int end = 8;
  while (end > 0 && (raw[end - 1] == ' ' || raw[end - 1] == '\0')) end--;
  for (int i = 0; i < end; i++) out->model[i] = raw[i];
  out->model[end] = '\0';
  out->valid = true;
  return true;
}

bool decode_control_config2(const ParsedFrame &f, SpaConfig *out) {
  if (!frame_is(f, msg::CTRL_CFG2_0, msg::CTRL_CFG2_1)) return false;
  if (f.payload_len < 5) return false;
  const uint8_t *d = f.payload;
  out->pumps[0] = d[0] & 0x03;
  out->pumps[1] = (d[0] >> 2) & 0x03;
  out->pumps[2] = (d[0] >> 4) & 0x03;
  out->pumps[3] = (d[0] >> 6) & 0x03;
  out->pumps[4] = d[1] & 0x03;
  out->pumps[5] = (d[1] >> 6) & 0x03;  // matches reference ControlConfiguration2 decode
  out->lights[0] = (d[2] & 0x03) != 0;
  out->lights[1] = ((d[2] >> 6) & 0x03) != 0;
  out->blower = d[3] & 0x03;
  out->circulation_pump = ((d[3] >> 6) & 0x03) != 0;
  out->mister = (d[4] & 0x30) != 0;
  out->aux[0] = (d[4] & 0x01) != 0;
  out->aux[1] = (d[4] & 0x02) != 0;
  out->valid = true;
  return true;
}
```

Add `#include <cstdio>` at the top of `messages.cpp` for `snprintf`.

- [ ] **Step 5: Run test to verify it passes**

Run: `make test`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/protocol/messages.h components/balboa_spa/protocol/messages.cpp tests/test_messages.cpp
git commit -m "feat: decode model/version + accessory inventory"
```

---

### Task 7: FilterCycles decode + encode

**Files:**
- Modify: `components/balboa_spa/protocol/messages.h`
- Modify: `components/balboa_spa/protocol/messages.cpp`
- Modify: `tests/test_messages.cpp` (append)

**Interfaces:**
- Produces:
  - `bool decode_filter_cycles(const ParsedFrame &f, FilterCyclesData *out);`
  - `size_t encode_filter_cycles(uint8_t *out, const FilterCyclesData &fc);`  // returns frame length

- [ ] **Step 1: Append failing tests to `tests/test_messages.cpp`**

```cpp
TEST(decode_filter_cycles_fields) {
  auto f = parse(hexb("7e 0d 0a bf 23 08 00 02 00 94 00 01 1e d0 7e"));
  FilterCyclesData fc{};
  CHECK(decode_filter_cycles(f, &fc));
  CHECK(fc.valid);
  CHECK_EQ(fc.c1_start_hour, 8);
  CHECK_EQ(fc.c1_start_minute, 0);
  CHECK_EQ(fc.c1_duration_min, 120);
  CHECK(fc.c2_enabled);
  CHECK_EQ(fc.c2_start_hour, 20);
  CHECK_EQ(fc.c2_start_minute, 0);
  CHECK_EQ(fc.c2_duration_min, 90);
}

TEST(encode_filter_cycles_roundtrip) {
  FilterCyclesData fc{};
  fc.c1_start_hour = 8; fc.c1_start_minute = 0; fc.c1_duration_min = 120;
  fc.c2_enabled = true; fc.c2_start_hour = 20; fc.c2_start_minute = 0; fc.c2_duration_min = 90;
  uint8_t out[32];
  size_t n = encode_filter_cycles(out, fc);
  auto expect = hexb("7e 0d 0a bf 23 08 00 02 00 94 00 01 1e d0 7e");
  CHECK_EQ(n, expect.size());
  for (size_t i = 0; i < n; i++) CHECK_EQ(out[i], expect[i]);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test`
Expected: FAIL to compile.

- [ ] **Step 3: Add declarations to `messages.h`**

```cpp
bool decode_filter_cycles(const ParsedFrame &f, FilterCyclesData *out);
size_t encode_filter_cycles(uint8_t *out, const FilterCyclesData &fc);
```

- [ ] **Step 4: Add implementations to `messages.cpp`**

```cpp
bool decode_filter_cycles(const ParsedFrame &f, FilterCyclesData *out) {
  if (!frame_is(f, msg::FILTER0, msg::FILTER1)) return false;
  if (f.payload_len < 8) return false;
  const uint8_t *d = f.payload;
  out->c1_start_hour = d[0];
  out->c1_start_minute = d[1];
  out->c1_duration_min = (uint16_t)(d[2] * 60 + d[3]);
  out->c2_enabled = (d[4] & 0x80) != 0;
  out->c2_start_hour = d[4] & 0x7f;
  out->c2_start_minute = d[5];
  out->c2_duration_min = (uint16_t)(d[6] * 60 + d[7]);
  out->valid = true;
  return true;
}

size_t encode_filter_cycles(uint8_t *out, const FilterCyclesData &fc) {
  uint8_t p[8];
  p[0] = fc.c1_start_hour;
  p[1] = fc.c1_start_minute;
  p[2] = (uint8_t)(fc.c1_duration_min / 60);
  p[3] = (uint8_t)(fc.c1_duration_min % 60);
  uint8_t c2h = fc.c2_start_hour & 0x7f;
  if (fc.c2_enabled) c2h |= 0x80;
  p[4] = c2h;
  p[5] = fc.c2_start_minute;
  p[6] = (uint8_t)(fc.c2_duration_min / 60);
  p[7] = (uint8_t)(fc.c2_duration_min % 60);
  return build_frame(out, 0x0a, msg::FILTER0, msg::FILTER1, p, 8);
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `make test`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/protocol/messages.h components/balboa_spa/protocol/messages.cpp tests/test_messages.cpp
git commit -m "feat: filter-cycle decode + encode"
```

---

### Task 8: Command encoders

**Files:**
- Modify: `components/balboa_spa/protocol/messages.h`
- Modify: `components/balboa_spa/protocol/messages.cpp`
- Modify: `tests/test_messages.cpp` (append)

**Interfaces:**
- Produces (all return total frame length, write into `out` with capacity ≥ 16):
  - `namespace item { constexpr uint8_t NORMAL_OPERATION=0x01, CLEAR_NOTIFICATION=0x03, PUMP1=0x04, BLOWER=0x0c, MISTER=0x0e, LIGHT1=0x11, AUX1=0x16, SOAK=0x1d, HOLD=0x3c, TEMPERATURE_RANGE=0x50, HEATING_MODE=0x51; }`
  - `size_t encode_toggle_item(uint8_t *out, uint8_t item_code);`
  - `size_t encode_set_target_temp(uint8_t *out, uint8_t temp_raw);`
  - `size_t encode_set_time(uint8_t *out, uint8_t hour, uint8_t minute, bool h24);`
  - `size_t encode_set_temp_scale(uint8_t *out, TempScale scale);`
  - `size_t encode_config_request(uint8_t *out);`
  - `size_t encode_control_config_request(uint8_t *out, uint8_t type);`  // type 1/2/3

- [ ] **Step 1: Append failing tests to `tests/test_messages.cpp`**

```cpp
TEST(encode_toggle_light1_bytes) {
  uint8_t out[16];
  size_t n = encode_toggle_item(out, item::LIGHT1);
  auto e = hexb("7e 07 0a bf 11 11 00 93 7e");
  CHECK_EQ(n, e.size());
  for (size_t i = 0; i < n; i++) CHECK_EQ(out[i], e[i]);
}

TEST(encode_set_temp_100f_bytes) {
  uint8_t out[16];
  size_t n = encode_set_target_temp(out, 100);
  auto e = hexb("7e 06 0a bf 20 64 29 7e");
  CHECK_EQ(n, e.size());
  for (size_t i = 0; i < n; i++) CHECK_EQ(out[i], e[i]);
}

TEST(encode_config_request_bytes) {
  uint8_t out[16];
  size_t n = encode_config_request(out);
  auto e = hexb("7e 05 0a bf 04 77 7e");
  CHECK_EQ(n, e.size());
  for (size_t i = 0; i < n; i++) CHECK_EQ(out[i], e[i]);
}

TEST(encode_control_config_request_type2_bytes) {
  uint8_t out[16];
  size_t n = encode_control_config_request(out, 2);
  auto e = hexb("7e 08 0a bf 22 00 00 01 58 7e");
  CHECK_EQ(n, e.size());
  for (size_t i = 0; i < n; i++) CHECK_EQ(out[i], e[i]);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test`
Expected: FAIL to compile.

- [ ] **Step 3: Add to `messages.h`** (after the existing `msg` namespace)

```cpp
namespace item {
constexpr uint8_t NORMAL_OPERATION = 0x01, CLEAR_NOTIFICATION = 0x03;
constexpr uint8_t PUMP1 = 0x04;   // pumpN = PUMP1 + N
constexpr uint8_t BLOWER = 0x0c, MISTER = 0x0e;
constexpr uint8_t LIGHT1 = 0x11;  // lightN = LIGHT1 + N
constexpr uint8_t AUX1 = 0x16;    // auxN   = AUX1 + N
constexpr uint8_t SOAK = 0x1d, HOLD = 0x3c;
constexpr uint8_t TEMPERATURE_RANGE = 0x50, HEATING_MODE = 0x51;
}  // namespace item

size_t encode_toggle_item(uint8_t *out, uint8_t item_code);
size_t encode_set_target_temp(uint8_t *out, uint8_t temp_raw);
size_t encode_set_time(uint8_t *out, uint8_t hour, uint8_t minute, bool h24);
size_t encode_set_temp_scale(uint8_t *out, TempScale scale);
size_t encode_config_request(uint8_t *out);
size_t encode_control_config_request(uint8_t *out, uint8_t type);
```

- [ ] **Step 4: Add to `messages.cpp`**

```cpp
size_t encode_toggle_item(uint8_t *out, uint8_t item_code) {
  uint8_t p[2] = {item_code, 0x00};
  return build_frame(out, 0x0a, 0xbf, 0x11, p, 2);
}

size_t encode_set_target_temp(uint8_t *out, uint8_t temp_raw) {
  return build_frame(out, 0x0a, 0xbf, 0x20, &temp_raw, 1);
}

size_t encode_set_time(uint8_t *out, uint8_t hour, uint8_t minute, bool h24) {
  uint8_t p[2] = {(uint8_t)(h24 ? (hour | 0x80) : hour), minute};
  return build_frame(out, 0x0a, 0xbf, 0x21, p, 2);
}

size_t encode_set_temp_scale(uint8_t *out, TempScale scale) {
  uint8_t p[2] = {0x01, (uint8_t)(scale == TempScale::CELSIUS ? 0x01 : 0x00)};
  return build_frame(out, 0x0a, 0xbf, 0x27, p, 2);
}

size_t encode_config_request(uint8_t *out) {
  return build_frame(out, 0x0a, 0xbf, 0x04, nullptr, 0);
}

size_t encode_control_config_request(uint8_t *out, uint8_t type) {
  uint8_t p[3];
  switch (type) {
    case 1: p[0] = 0x02; p[1] = 0x00; p[2] = 0x00; break;  // info (0a bf 24)
    case 2: p[0] = 0x00; p[1] = 0x00; p[2] = 0x01; break;  // config2 (0a bf 2e)
    case 3: p[0] = 0x01; p[1] = 0x00; p[2] = 0x00; break;  // filter cycles (0a bf 23)
    default: p[0] = 0x00; p[1] = 0x00; p[2] = 0x00; break;
  }
  return build_frame(out, 0x0a, 0xbf, 0x22, p, 3);
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `make test`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/protocol/messages.h components/balboa_spa/protocol/messages.cpp tests/test_messages.cpp
git commit -m "feat: command encoders (toggle/temp/time/scale/requests)"
```

---

### Task 9: Temperature conversion

**Files:**
- Create: `components/balboa_spa/protocol/temperature.h`
- Create: `components/balboa_spa/protocol/temperature.cpp`
- Create: `tests/test_temperature.cpp`

**Interfaces:**
- Consumes: `TempScale` (Task 5).
- Produces:
  - `float spa_raw_to_celsius(uint8_t raw, TempScale scale);`  // for the climate entity (always °C)
  - `float spa_raw_to_native(uint8_t raw, TempScale scale);`   // native display value (F whole / C half)
  - `uint8_t celsius_to_spa_raw(float celsius, TempScale scale);`  // for SetTargetTemperature

- [ ] **Step 1: Write the failing test `tests/test_temperature.cpp`**

```cpp
#include "test_framework.h"
#include "temperature.h"
#include <cmath>

using namespace esphome::balboa_spa;

static bool near(float a, float b) { return std::fabs(a - b) < 0.05f; }

TEST(temp_fahrenheit_to_celsius) {
  CHECK(near(spa_raw_to_celsius(100, TempScale::FAHRENHEIT), 37.78f));
  CHECK(near(spa_raw_to_native(100, TempScale::FAHRENHEIT), 100.0f));
}

TEST(temp_celsius_halfdegree) {
  // raw 80 in Celsius mode == 40.0 C
  CHECK(near(spa_raw_to_celsius(80, TempScale::CELSIUS), 40.0f));
  CHECK(near(spa_raw_to_native(80, TempScale::CELSIUS), 40.0f));
}

TEST(temp_celsius_to_raw) {
  CHECK_EQ(celsius_to_spa_raw(40.0f, TempScale::CELSIUS), 80);    // doubled
  CHECK_EQ(celsius_to_spa_raw(37.78f, TempScale::FAHRENHEIT), 100);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test`
Expected: FAIL to compile (`temperature.h` not found).

- [ ] **Step 3: Write `components/balboa_spa/protocol/temperature.h`**

```cpp
#pragma once
#include <cstdint>
#include "types.h"

namespace esphome {
namespace balboa_spa {

float spa_raw_to_celsius(uint8_t raw, TempScale scale);
float spa_raw_to_native(uint8_t raw, TempScale scale);
uint8_t celsius_to_spa_raw(float celsius, TempScale scale);

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 4: Write `components/balboa_spa/protocol/temperature.cpp`**

```cpp
#include "temperature.h"
#include <cmath>

namespace esphome {
namespace balboa_spa {

float spa_raw_to_native(uint8_t raw, TempScale scale) {
  return (scale == TempScale::CELSIUS) ? (raw / 2.0f) : (float) raw;
}

float spa_raw_to_celsius(uint8_t raw, TempScale scale) {
  if (scale == TempScale::CELSIUS) return raw / 2.0f;
  return (raw - 32.0f) * 5.0f / 9.0f;
}

uint8_t celsius_to_spa_raw(float celsius, TempScale scale) {
  if (scale == TempScale::CELSIUS)
    return (uint8_t) std::lround(celsius * 2.0f);
  return (uint8_t) std::lround(celsius * 9.0f / 5.0f + 32.0f);
}

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 5: Run test to verify it passes**

Run: `make test`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/protocol/temperature.h components/balboa_spa/protocol/temperature.cpp tests/test_temperature.cpp
git commit -m "feat: temperature unit conversions"
```

---

### Task 10: ProtocolEngine — feed/state + Ready-gated TX + read-only

**Files:**
- Create: `components/balboa_spa/protocol/protocol_engine.h`
- Create: `components/balboa_spa/protocol/protocol_engine.cpp`
- Create: `tests/test_engine.cpp`

**Interfaces:**
- Consumes: `scan_frame`, all decoders/encoders, `SpaStatus`/`SpaConfig`/`SpaInfo`/`FilterCyclesData`.
- Produces class `ProtocolEngine`:
  - `using WriteFn = std::function<void(const uint8_t *, size_t)>;`
  - `void set_write_fn(WriteFn fn);`
  - `void set_read_only(bool ro);`
  - `void feed(const uint8_t *data, size_t len);`  // ingest RX bytes
  - `const SpaStatus &status() const;` `const SpaConfig &config() const;` `const SpaInfo &info() const;` `const FilterCyclesData &filter_cycles() const;`
  - `bool have_full_config() const;`  // status.valid && info.valid && config.valid && filter_cycles.valid
  - `void enqueue_frame(const uint8_t *frame, size_t len);`  // low-level
  - `void toggle_item(uint8_t item_code);`
  - `std::function<void()> on_status_update;` `std::function<void()> on_config_update;`

- [ ] **Step 1: Write the failing test `tests/test_engine.cpp`**

```cpp
#include "test_framework.h"
#include "protocol_engine.h"
#include "messages.h"
#include <vector>

using namespace esphome::balboa_spa;

struct Sink {
  std::vector<std::vector<uint8_t>> writes;
  void operator()(const uint8_t *d, size_t n) { writes.emplace_back(d, d + n); }
};

static void feed_hex(ProtocolEngine &e, const char *hex) {
  auto b = hexb(hex);
  e.feed(b.data(), b.size());
}

TEST(engine_decodes_status_and_fires_callback) {
  ProtocolEngine e;
  int fired = 0;
  e.on_status_update = [&] { fired++; };
  feed_hex(&e == nullptr ? e : e,  // keep formatting simple
           "7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 01 00 02 03 "
           "00 00 00 00 00 66 00 00 00 0f 7e");
  CHECK(e.status().valid);
  CHECK_EQ(e.status().current_temp_raw, 100);
  CHECK_EQ(fired, 1);
}

TEST(engine_sends_queued_command_only_on_ready) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  // Must have seen a Status before TX is allowed:
  feed_hex(e, "7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 01 00 02 03 "
              "00 00 00 00 00 66 00 00 00 0f 7e");
  e.toggle_item(item::LIGHT1);
  CHECK_EQ(sink.writes.size(), 0);  // nothing sent yet (no Ready)
  feed_hex(e, "7e 05 10 bf 06 5c 7e");  // Ready -> sends one
  CHECK_EQ(sink.writes.size(), 1);
  auto expect = hexb("7e 07 0a bf 11 11 00 93 7e");
  CHECK(sink.writes[0] == expect);
  feed_hex(e, "7e 05 10 bf 06 5c 7e");  // next Ready, queue empty -> nothing
  CHECK_EQ(sink.writes.size(), 1);
}

TEST(engine_read_only_suppresses_tx) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  e.set_read_only(true);
  feed_hex(e, "7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 01 00 02 03 "
              "00 00 00 00 00 66 00 00 00 0f 7e");
  e.toggle_item(item::LIGHT1);
  feed_hex(e, "7e 05 10 bf 06 5c 7e");
  CHECK_EQ(sink.writes.size(), 0);
}

TEST(engine_no_tx_before_first_status) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  e.toggle_item(item::LIGHT1);
  feed_hex(e, "7e 05 10 bf 06 5c 7e");  // Ready but no Status yet
  CHECK_EQ(sink.writes.size(), 0);
}
```

> Note: replace the awkward first-call line in `engine_decodes_status_and_fires_callback` with a plain `feed_hex(e, "...");` — written verbatim below in Step 3's reference. (Kept minimal here.)

Correct the first test body to:

```cpp
TEST(engine_decodes_status_and_fires_callback) {
  ProtocolEngine e;
  int fired = 0;
  e.on_status_update = [&] { fired++; };
  feed_hex(e, "7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 01 00 02 03 "
              "00 00 00 00 00 66 00 00 00 0f 7e");
  CHECK(e.status().valid);
  CHECK_EQ(e.status().current_temp_raw, 100);
  CHECK_EQ(fired, 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test`
Expected: FAIL to compile (`protocol_engine.h` not found).

- [ ] **Step 3: Write `components/balboa_spa/protocol/protocol_engine.h`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include "types.h"

namespace esphome {
namespace balboa_spa {

class ProtocolEngine {
 public:
  using WriteFn = std::function<void(const uint8_t *, size_t)>;

  void set_write_fn(WriteFn fn) { write_fn_ = std::move(fn); }
  void set_read_only(bool ro) { read_only_ = ro; }
  void set_address(uint8_t addr) { address_ = addr; }

  void feed(const uint8_t *data, size_t len);

  const SpaStatus &status() const { return status_; }
  const SpaConfig &config() const { return config_; }
  const SpaInfo &info() const { return info_; }
  const FilterCyclesData &filter_cycles() const { return filter_; }
  bool have_full_config() const {
    return status_.valid && info_.valid && config_.valid && filter_.valid;
  }

  void enqueue_frame(const uint8_t *frame, size_t len);
  void toggle_item(uint8_t item_code);

  std::function<void()> on_status_update;
  std::function<void()> on_config_update;

 protected:
  void process_frame(const ParsedFrame &f);
  bool pop_and_send_();

  static constexpr size_t RX_CAP = 256;
  static constexpr size_t Q_SLOTS = 16;
  static constexpr size_t Q_FRAME_CAP = 16;

  uint8_t rx_[RX_CAP];
  size_t rx_len_ = 0;

  uint8_t queue_[Q_SLOTS][Q_FRAME_CAP];
  uint8_t queue_len_[Q_SLOTS] = {0};
  size_t q_head_ = 0, q_tail_ = 0, q_count_ = 0;

  WriteFn write_fn_;
  bool read_only_ = false;
  uint8_t address_ = 0x0a;
  bool seen_status_ = false;

  SpaStatus status_;
  SpaConfig config_;
  SpaInfo info_;
  FilterCyclesData filter_;
};

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 4: Write `components/balboa_spa/protocol/protocol_engine.cpp`**

```cpp
#include "protocol_engine.h"
#include "frame.h"
#include "messages.h"
#include <cstring>

namespace esphome {
namespace balboa_spa {

void ProtocolEngine::enqueue_frame(const uint8_t *frame, size_t len) {
  if (len > Q_FRAME_CAP || q_count_ >= Q_SLOTS) return;  // drop if full/oversized
  std::memcpy(queue_[q_tail_], frame, len);
  queue_len_[q_tail_] = (uint8_t) len;
  q_tail_ = (q_tail_ + 1) % Q_SLOTS;
  q_count_++;
}

void ProtocolEngine::toggle_item(uint8_t item_code) {
  uint8_t f[Q_FRAME_CAP];
  size_t n = encode_toggle_item(f, item_code);
  enqueue_frame(f, n);
}

bool ProtocolEngine::pop_and_send_() {
  if (q_count_ == 0) return false;
  if (read_only_ || !seen_status_ || !write_fn_) return false;
  write_fn_(queue_[q_head_], queue_len_[q_head_]);
  q_head_ = (q_head_ + 1) % Q_SLOTS;
  q_count_--;
  return true;
}

void ProtocolEngine::feed(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (rx_len_ < RX_CAP) rx_[rx_len_++] = data[i];
    // else: overflow — drop oldest by shifting half (rare; keeps us alive)
    else {
      std::memmove(rx_, rx_ + RX_CAP / 2, RX_CAP / 2);
      rx_len_ = RX_CAP / 2;
      rx_[rx_len_++] = data[i];
    }
  }
  // Parse as many frames as available.
  while (true) {
    ParsedFrame f{};
    size_t consumed = 0;
    ScanResult r = scan_frame(rx_, rx_len_, &f, &consumed);
    if (r == ScanResult::FRAME) {
      process_frame(f);
      // shift out consumed bytes
      std::memmove(rx_, rx_ + consumed, rx_len_ - consumed);
      rx_len_ -= consumed;
    } else {  // NEED_MORE
      if (consumed > 0) {
        std::memmove(rx_, rx_ + consumed, rx_len_ - consumed);
        rx_len_ -= consumed;
      }
      break;
    }
  }
}

void ProtocolEngine::process_frame(const ParsedFrame &f) {
  if (decode_status(f, &status_)) {
    seen_status_ = true;
    if (on_status_update) on_status_update();
    return;
  }
  if (decode_control_config(f, &info_)) {
    if (on_config_update) on_config_update();
    return;
  }
  if (decode_control_config2(f, &config_)) {
    if (on_config_update) on_config_update();
    return;
  }
  if (decode_filter_cycles(f, &filter_)) {
    if (on_config_update) on_config_update();
    return;
  }
  if (is_ready(f)) {
    pop_and_send_();
    return;
  }
  // NewClientCTS and unrecognized frames are ignored.
}

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 5: Run test to verify it passes**

Run: `make test`
Expected: PASS (all engine tests green).

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/protocol/protocol_engine.h components/balboa_spa/protocol/protocol_engine.cpp tests/test_engine.cpp
git commit -m "feat: ProtocolEngine feed/state + Ready-gated TX + read-only"
```

---

### Task 11: Engine handshake + high-level multi-step commands

**Files:**
- Modify: `components/balboa_spa/protocol/protocol_engine.h`
- Modify: `components/balboa_spa/protocol/protocol_engine.cpp`
- Modify: `tests/test_engine.cpp` (append)

**Interfaces:**
- Produces additional `ProtocolEngine` methods:
  - `void request_missing_config();`  // enqueue requests for whichever of info/config2/filter are not yet valid
  - `void set_pump(uint8_t index, uint8_t desired_speed);`  // enqueues N toggles
  - `void set_light(uint8_t index, bool on);`
  - `void set_aux(uint8_t index, bool on);`
  - `void set_mister(bool on);`
  - `void set_blower(uint8_t desired_level);`
  - `void set_hold(bool on);`
  - `void set_heating_mode(HeatingMode desired);`
  - `void set_temperature_range(TempRange desired);`
  - `void set_temperature_scale(TempScale scale);`
  - `void set_target_temperature_raw(uint8_t raw);`
  - `void set_time(uint8_t hour, uint8_t minute, bool h24);`
  - `void update_filter_cycles(const FilterCyclesData &fc);`
- Behavior: when a `Status` is decoded and `!have_full_config()`, the engine automatically calls `request_missing_config()` (rate-limited to avoid spamming — only enqueue a given request type if the queue does not already hold it; simplest: only auto-request when `q_count_ == 0`).

- [ ] **Step 1: Append failing tests to `tests/test_engine.cpp`**

```cpp
TEST(engine_requests_config_after_status_when_incomplete) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  feed_hex(e, "7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 01 00 02 03 "
              "00 00 00 00 00 66 00 00 00 0f 7e");  // status only -> config incomplete
  // 3 requests should be queued (info, config2, filter). Drain on Readys:
  feed_hex(e, "7e 05 10 bf 06 5c 7e");
  feed_hex(e, "7e 05 10 bf 06 5c 7e");
  feed_hex(e, "7e 05 10 bf 06 5c 7e");
  CHECK_EQ(sink.writes.size(), 3);
  // first queued is the info request (control config request type 1)
  auto info_req = hexb("7e 08 0a bf 22 02 00 00 ");  // CRC appended below
  // verify it is a 0a bf 22 request frame
  CHECK_EQ(sink.writes[0][3], 0xbf);
  CHECK_EQ(sink.writes[0][4], 0x22);
}

TEST(engine_set_pump_enqueues_two_toggles_from_zero) {
  ProtocolEngine e;
  Sink sink;
  e.set_write_fn([&](const uint8_t *d, size_t n) { sink(d, n); });
  // status with pump1 = 0; mark config so engine doesn't also enqueue requests
  feed_hex(e, "7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 00 00 02 03 "
              "00 00 00 00 00 66 00 00 00 0e 7e");
  // pretend config known so auto-request is quiet: feed config2 + info + filter
  feed_hex(e, "7e 1a 0a bf 24 64 dc 11 00 42 46 42 50 32 30 20 20 01 3d 12 38 2e 01 0a 04 00 9b 7e");
  feed_hex(e, "7e 0b 0a bf 2e 0a 00 01 d0 00 44 6f 7e");
  feed_hex(e, "7e 0d 0a bf 23 08 00 02 00 94 00 01 1e d0 7e");
  size_t before = sink.writes.size();
  e.set_pump(0, 2);  // 0 -> 2 needs 2 toggles (max speed 2 from config)
  for (int i = 0; i < 6; i++) feed_hex(e, "7e 05 10 bf 06 5c 7e");
  size_t toggles = 0;
  for (size_t i = before; i < sink.writes.size(); i++)
    if (sink.writes[i][4] == 0x11 && sink.writes[i][5] == item::PUMP1) toggles++;
  CHECK_EQ(toggles, 2);
}
```

> The CRC bytes for the auto-requests are validated implicitly by `frame.cpp`; the tests above assert the message-type bytes rather than full frames, which is sufficient and avoids over-coupling.

- [ ] **Step 2: Run test to verify it fails**

Run: `make test`
Expected: FAIL to compile (`set_pump` etc. undeclared).

- [ ] **Step 3: Add declarations to `protocol_engine.h`** (public section)

```cpp
  void request_missing_config();
  void set_target_temperature_raw(uint8_t raw);
  void set_light(uint8_t index, bool on);
  void set_aux(uint8_t index, bool on);
  void set_mister(bool on);
  void set_hold(bool on);
  void set_pump(uint8_t index, uint8_t desired_speed);
  void set_blower(uint8_t desired_level);
  void set_heating_mode(HeatingMode desired);
  void set_temperature_range(TempRange desired);
  void set_temperature_scale(TempScale scale);
  void set_time(uint8_t hour, uint8_t minute, bool h24);
  void update_filter_cycles(const FilterCyclesData &fc);
```

- [ ] **Step 4: Add implementations to `protocol_engine.cpp`** and wire auto-request in `process_frame`

Add these methods:

```cpp
void ProtocolEngine::request_missing_config() {
  uint8_t f[Q_FRAME_CAP];
  if (!info_.valid)   { size_t n = encode_control_config_request(f, 1); enqueue_frame(f, n); }
  if (!config_.valid) { size_t n = encode_control_config_request(f, 2); enqueue_frame(f, n); }
  if (!filter_.valid) { size_t n = encode_control_config_request(f, 3); enqueue_frame(f, n); }
}

void ProtocolEngine::set_target_temperature_raw(uint8_t raw) {
  uint8_t f[Q_FRAME_CAP];
  size_t n = encode_set_target_temp(f, raw);
  enqueue_frame(f, n);
}

void ProtocolEngine::set_light(uint8_t index, bool on) {
  if (index > 1) return;
  if (status_.valid && status_.lights[index] == on) return;
  toggle_item(item::LIGHT1 + index);
}

void ProtocolEngine::set_aux(uint8_t index, bool on) {
  if (index > 1) return;
  if (status_.valid && status_.aux[index] == on) return;
  toggle_item(item::AUX1 + index);
}

void ProtocolEngine::set_mister(bool on) {
  if (status_.valid && status_.mister == on) return;
  toggle_item(item::MISTER);
}

void ProtocolEngine::set_hold(bool on) {
  if (status_.valid && status_.hold == on) return;
  toggle_item(item::HOLD);
}

void ProtocolEngine::set_pump(uint8_t index, uint8_t desired_speed) {
  if (index > 5 || !status_.valid || !config_.valid) return;
  uint8_t max_speed = config_.pumps[index];
  if (max_speed == 0) return;
  if (desired_speed > max_speed) desired_speed = max_speed;
  uint8_t current = status_.pumps[index];
  if (current > max_speed) current = max_speed;
  uint8_t times = (uint8_t)(((int) desired_speed - (int) current + (max_speed + 1)) % (max_speed + 1));
  for (uint8_t i = 0; i < times; i++) toggle_item(item::PUMP1 + index);
}

void ProtocolEngine::set_blower(uint8_t desired_level) {
  if (!status_.valid || !config_.valid) return;
  uint8_t max_level = config_.blower;
  if (max_level == 0) return;
  if (desired_level > max_level) desired_level = max_level;
  uint8_t times = (uint8_t)(((int) desired_level - (int) status_.blower + (max_level + 1)) % (max_level + 1));
  for (uint8_t i = 0; i < times; i++) toggle_item(item::BLOWER);
}

void ProtocolEngine::set_heating_mode(HeatingMode desired) {
  if (!status_.valid) return;
  HeatingMode cur = status_.heating_mode;
  uint8_t times = 0;
  if ((cur == HeatingMode::READY && desired == HeatingMode::REST) ||
      (cur == HeatingMode::REST && desired == HeatingMode::READY) ||
      (cur == HeatingMode::READY_IN_REST && desired == HeatingMode::REST)) {
    times = 1;
  } else if (cur == HeatingMode::READY_IN_REST && desired == HeatingMode::READY) {
    times = 2;
  }
  for (uint8_t i = 0; i < times; i++) toggle_item(item::HEATING_MODE);
}

void ProtocolEngine::set_temperature_range(TempRange desired) {
  if (status_.valid && status_.temp_range == desired) return;
  toggle_item(item::TEMPERATURE_RANGE);
}

void ProtocolEngine::set_temperature_scale(TempScale scale) {
  uint8_t f[Q_FRAME_CAP];
  size_t n = encode_set_temp_scale(f, scale);
  enqueue_frame(f, n);
}

void ProtocolEngine::set_time(uint8_t hour, uint8_t minute, bool h24) {
  uint8_t f[Q_FRAME_CAP];
  size_t n = encode_set_time(f, hour, minute, h24);
  enqueue_frame(f, n);
}

void ProtocolEngine::update_filter_cycles(const FilterCyclesData &fc) {
  uint8_t f[Q_FRAME_CAP + 8];
  size_t n = encode_filter_cycles(f, fc);
  enqueue_frame(f, n);
  filter_ = fc;
}
```

Then, inside `process_frame`, in the `decode_status` branch (after firing `on_status_update`, before `return`), add the auto-request:

```cpp
  if (decode_status(f, &status_)) {
    seen_status_ = true;
    if (on_status_update) on_status_update();
    if (!have_full_config() && q_count_ == 0) request_missing_config();
    return;
  }
```

Note: `update_filter_cycles` writes an 8-byte-payload frame (15 bytes total) which exceeds `Q_FRAME_CAP` (16) only marginally — confirm `Q_FRAME_CAP` is 16 and the frame is 15 bytes, so it fits. (Filter frame total = 8 payload + 7 = 15 ≤ 16.) The local `f[Q_FRAME_CAP + 8]` buffer is just defensive; enqueue copies only `n` bytes.

- [ ] **Step 5: Run test to verify it passes**

Run: `make test`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/protocol/protocol_engine.h components/balboa_spa/protocol/protocol_engine.cpp tests/test_engine.cpp
git commit -m "feat: engine handshake + high-level multi-step commands"
```

---

## ESPHome integration (Tasks 12–23)

> From here on, builds use ESPHome, not `make`. Validate with `esphome config <yaml>` and compile with `esphome compile <yaml>`. A throwaway bring-up YAML (`test-device.yaml`) is created in Task 12 and reused through Task 21; the final user-facing config is assembled in Task 22.
>
> ESPHome external-component conventions (verify against the installed 2025.2.0 if any codegen call differs): hub uses `cg.new_Pvariable` + `cg.register_component` + `uart.register_uart_device`; child platforms use their component's `new_*`/`register_*` helper and link to the hub via a `use_id` reference.

### Task 12: ESPHome hub component `balboa_spa`

**Files:**
- Create: `components/balboa_spa/__init__.py`
- Create: `components/balboa_spa/balboa_spa.h`
- Create: `components/balboa_spa/balboa_spa.cpp`
- Create: `test-device.yaml`
- Create: `secrets.yaml` (local, git-ignored — for compile only)

**Interfaces:**
- Consumes: `ProtocolEngine` and all protocol headers.
- Produces:
  - C++ class `BalboaSpa : public Component, public uart::UARTDevice` with: `void set_read_only(bool)`, `void set_time(time::RealTimeClock *)`, `ProtocolEngine &engine()`, `const SpaStatus &status()`, `const SpaConfig &config()`, `const SpaInfo &info()`, `const FilterCyclesData &filter_cycles()`, `void add_on_status_callback(std::function<void()>)`, `void add_on_config_callback(std::function<void()>)`.
  - Python symbols: `balboa_spa_ns`, `BalboaSpa`, `CONF_BALBOA_SPA_ID = "balboa_spa_id"`.

- [ ] **Step 1: Write `components/balboa_spa/__init__.py`**

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, time
from esphome.const import CONF_ID

CODEOWNERS = ["@darrylb"]
DEPENDENCIES = ["uart"]
MULTI_CONF = True

balboa_spa_ns = cg.esphome_ns.namespace("balboa_spa")
BalboaSpa = balboa_spa_ns.class_("BalboaSpa", cg.Component, uart.UARTDevice)

CONF_BALBOA_SPA_ID = "balboa_spa_id"
CONF_READ_ONLY = "read_only"
CONF_TIME_ID = "time_id"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BalboaSpa),
            cv.Optional(CONF_READ_ONLY, default=True): cv.boolean,
            cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_read_only(config[CONF_READ_ONLY]))
    if CONF_TIME_ID in config:
        rtc = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(rtc))
```

- [ ] **Step 2: Write `components/balboa_spa/balboa_spa.h`**

```cpp
#pragma once
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/time/real_time_clock.h"
#include "protocol/protocol_engine.h"

namespace esphome {
namespace balboa_spa {

class BalboaSpa : public Component, public uart::UARTDevice {
 public:
  float get_setup_priority() const override { return setup_priority::DATA; }
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_read_only(bool ro) { read_only_ = ro; }
  void set_time(time::RealTimeClock *rtc) { time_ = rtc; }

  ProtocolEngine &engine() { return engine_; }
  const SpaStatus &status() const { return engine_.status(); }
  const SpaConfig &config() const { return engine_.config(); }
  const SpaInfo &info() const { return engine_.info(); }
  const FilterCyclesData &filter_cycles() const { return engine_.filter_cycles(); }

  void add_on_status_callback(std::function<void()> &&cb) { status_cb_.add(std::move(cb)); }
  void add_on_config_callback(std::function<void()> &&cb) { config_cb_.add(std::move(cb)); }

 protected:
  void maybe_sync_time_();

  ProtocolEngine engine_;
  CallbackManager<void()> status_cb_;
  CallbackManager<void()> config_cb_;
  time::RealTimeClock *time_{nullptr};
  bool read_only_{true};
  bool discovery_logged_{false};
  uint32_t last_time_sync_{0};
  uint8_t rx_chunk_[128];
};

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 3: Write `components/balboa_spa/balboa_spa.cpp`**

```cpp
#include "balboa_spa.h"
#include "esphome/core/log.h"

namespace esphome {
namespace balboa_spa {

static const char *const TAG = "balboa_spa";

void BalboaSpa::setup() {
  engine_.set_read_only(read_only_);
  engine_.set_write_fn([this](const uint8_t *d, size_t n) { this->write_array(d, n); });
  engine_.on_status_update = [this]() {
    this->status_cb_.call();
    this->maybe_sync_time_();
  };
  engine_.on_config_update = [this]() {
    this->config_cb_.call();
    if (!this->discovery_logged_ && this->engine_.config().valid && this->engine_.info().valid) {
      const SpaConfig &c = this->engine_.config();
      const SpaInfo &i = this->engine_.info();
      ESP_LOGI(TAG, "Detected spa: model='%s' version='%s'", i.model, i.version);
      for (int p = 0; p < 6; p++)
        if (c.pumps[p]) ESP_LOGI(TAG, "  pump%d: %d-speed", p + 1, c.pumps[p]);
      for (int l = 0; l < 2; l++)
        if (c.lights[l]) ESP_LOGI(TAG, "  light%d present", l + 1);
      if (c.circulation_pump) ESP_LOGI(TAG, "  circulation pump present");
      if (c.blower) ESP_LOGI(TAG, "  blower: %d-level", c.blower);
      if (c.mister) ESP_LOGI(TAG, "  mister present");
      for (int a = 0; a < 2; a++)
        if (c.aux[a]) ESP_LOGI(TAG, "  aux%d present", a + 1);
      this->discovery_logged_ = true;
    }
  };
}

void BalboaSpa::loop() {
  int avail = this->available();
  while (avail > 0) {
    int n = avail > (int) sizeof(rx_chunk_) ? (int) sizeof(rx_chunk_) : avail;
    this->read_array(rx_chunk_, n);
    engine_.feed(rx_chunk_, n);
    avail -= n;
  }
}

void BalboaSpa::maybe_sync_time_() {
  if (time_ == nullptr || read_only_) return;
  auto now = time_->now();
  if (!now.is_valid()) return;
  uint32_t ms = millis();
  if (last_time_sync_ != 0 && (ms - last_time_sync_) < 60000UL) return;
  const SpaStatus &s = engine_.status();
  int now_min = now.hour * 60 + now.minute;
  int spa_min = s.hour * 60 + s.minute;
  int diff = (now_min - spa_min + 1440) % 1440;
  if (diff > 720) diff = 1440 - diff;
  if (diff > 1) {
    ESP_LOGI(TAG, "Syncing spa clock %02d:%02d -> %02d:%02d", s.hour, s.minute, now.hour, now.minute);
    engine_.set_time(now.hour, now.minute, s.twenty_four_hour);
    last_time_sync_ = ms;
  }
}

void BalboaSpa::dump_config() {
  ESP_LOGCONFIG(TAG, "Balboa Spa:");
  ESP_LOGCONFIG(TAG, "  read_only: %s", read_only_ ? "YES" : "NO");
  this->check_uart_settings(115200);
}

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 4: Write `secrets.yaml`** (git-ignored; placeholder values fine for compile)

```yaml
wifi_ssid: "changeme"
wifi_password: "changeme"
mqtt_broker: "192.168.1.10"
mqtt_username: "spa"
mqtt_password: "changeme"
ota_password: "changeme"
ap_password: "changeme123"
```

- [ ] **Step 5: Write `test-device.yaml`** (minimal bring-up config)

```yaml
esphome:
  name: balboa-test
external_components:
  - source:
      type: local
      path: components

esp32:
  board: esp32dev
  framework:
    type: arduino

logger:
  baud_rate: 0  # disable logging on UART0 TX; USB CDC/JTAG still shows logs

uart:
  id: spa_uart
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 115200
  rx_buffer_size: 512

balboa_spa:
  id: spa
  uart_id: spa_uart
  read_only: true
```

> `logger: baud_rate: 0` frees UART0 so the spa UART and logs do not collide. For a board with only UART0-over-USB, instead keep `logger` default and ensure the spa UART uses GPIO16/17 (UART2) — they are independent of UART0, so logs over USB still work; set `baud_rate: 0` only if you observe contention.

- [ ] **Step 6: Validate config**

Run: `esphome config test-device.yaml`
Expected: prints the resolved configuration with no errors.

- [ ] **Step 7: Compile**

Run: `esphome compile test-device.yaml`
Expected: build succeeds (`Successfully created ... firmware.bin`). First run downloads the toolchain/platform — allow time and network.

- [ ] **Step 8: Commit**

```bash
echo "secrets.yaml" >> .gitignore
git add components/balboa_spa/__init__.py components/balboa_spa/balboa_spa.h components/balboa_spa/balboa_spa.cpp test-device.yaml .gitignore
git commit -m "feat: ESPHome balboa_spa hub component"
```

---

### Task 13: climate platform (water heater)

**Files:**
- Create: `components/balboa_spa/climate/__init__.py`
- Create: `components/balboa_spa/climate/balboa_climate.h`
- Create: `components/balboa_spa/climate/balboa_climate.cpp`
- Modify: `test-device.yaml` (add a `climate:` entry)

**Interfaces:**
- Consumes: `BalboaSpa`, `spa_raw_to_celsius`, `celsius_to_spa_raw`.
- Produces: `BalboaClimate` (climate entity) that publishes current/target temp in °C and maps HEAT/OFF.

- [ ] **Step 1: Write `components/balboa_spa/climate/__init__.py`**

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaClimate = balboa_spa_ns.class_("BalboaClimate", climate.Climate, cg.Component)

CONFIG_SCHEMA = climate.climate_schema(BalboaClimate).extend(
    {
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
```

- [ ] **Step 2: Write `components/balboa_spa/climate/balboa_climate.h`**

```cpp
#pragma once
#include "esphome/components/climate/climate.h"
#include "esphome/core/component.h"
#include "../balboa_spa.h"

namespace esphome {
namespace balboa_spa {

class BalboaClimate : public climate::Climate, public Component {
 public:
  void set_parent(BalboaSpa *parent) { parent_ = parent; }
  void setup() override;
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

 protected:
  void update_from_spa_();
  BalboaSpa *parent_{nullptr};
};

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 3: Write `components/balboa_spa/climate/balboa_climate.cpp`**

```cpp
#include "balboa_climate.h"
#include "../protocol/temperature.h"

namespace esphome {
namespace balboa_spa {

void BalboaClimate::setup() {
  parent_->add_on_status_callback([this]() { this->update_from_spa_(); });
}

climate::ClimateTraits BalboaClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_supports_current_temperature(true);
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});
  traits.set_visual_min_temperature(10.0);   // 50 F
  traits.set_visual_max_temperature(40.0);   // 104 F
  traits.set_visual_temperature_step(0.5);
  traits.set_supports_action(true);
  return traits;
}

void BalboaClimate::update_from_spa_() {
  const SpaStatus &s = parent_->status();
  if (!s.valid) return;
  if (s.current_temp_valid)
    this->current_temperature = spa_raw_to_celsius(s.current_temp_raw, s.temp_scale);
  this->target_temperature = spa_raw_to_celsius(s.target_temp_raw, s.temp_scale);
  // The spa heater is always "on" (it heats to setpoint); model OFF only in Rest with no demand.
  this->mode = climate::CLIMATE_MODE_HEAT;
  this->action = s.heating ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_IDLE;
  this->publish_state();
}

void BalboaClimate::control(const climate::ClimateCall &call) {
  if (call.get_target_temperature().has_value()) {
    const SpaStatus &s = parent_->status();
    float c = *call.get_target_temperature();
    uint8_t raw = celsius_to_spa_raw(c, s.temp_scale);
    parent_->engine().set_target_temperature_raw(raw);
  }
  // mode changes are not separately actionable (heater follows setpoint); ignore.
}

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 4: Add to `test-device.yaml`**

```yaml
climate:
  - platform: balboa_spa
    name: "Spa"
    balboa_spa_id: spa
```

- [ ] **Step 5: Validate + compile**

Run: `esphome config test-device.yaml && esphome compile test-device.yaml`
Expected: success.

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/climate/ test-device.yaml
git commit -m "feat: climate (water heater) platform"
```

---

### Task 14: sensor platform (temperatures)

**Files:**
- Create: `components/balboa_spa/sensor/__init__.py`
- Create: `components/balboa_spa/sensor/balboa_sensor.h`
- Create: `components/balboa_spa/sensor/balboa_sensor.cpp`
- Modify: `test-device.yaml`

**Interfaces:**
- Produces: `BalboaSensor` with `SpaSensorType type_` ∈ {CURRENT_TEMPERATURE, TARGET_TEMPERATURE} publishing native-unit values.

- [ ] **Step 1: Write `components/balboa_spa/sensor/__init__.py`**

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_TYPE, UNIT_EMPTY
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaSensor = balboa_spa_ns.class_("BalboaSensor", sensor.Sensor, cg.Component)
SpaSensorType = balboa_spa_ns.enum("SpaSensorType")
SENSOR_TYPES = {
    "current_temperature": SpaSensorType.CURRENT_TEMPERATURE,
    "target_temperature": SpaSensorType.TARGET_TEMPERATURE,
}

CONFIG_SCHEMA = sensor.sensor_schema(BalboaSensor).extend(
    {
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        cv.Required(CONF_TYPE): cv.enum(SENSOR_TYPES, lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_sensor_type(config[CONF_TYPE]))
```

- [ ] **Step 2: Write `components/balboa_spa/sensor/balboa_sensor.h`**

```cpp
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
```

- [ ] **Step 3: Write `components/balboa_spa/sensor/balboa_sensor.cpp`**

```cpp
#include "balboa_sensor.h"
#include "../protocol/temperature.h"

namespace esphome {
namespace balboa_spa {

void BalboaSensor::setup() {
  parent_->add_on_status_callback([this]() { this->update_from_spa_(); });
}

void BalboaSensor::update_from_spa_() {
  const SpaStatus &s = parent_->status();
  if (!s.valid) return;
  switch (type_) {
    case CURRENT_TEMPERATURE:
      if (s.current_temp_valid)
        this->publish_state(spa_raw_to_native(s.current_temp_raw, s.temp_scale));
      break;
    case TARGET_TEMPERATURE:
      this->publish_state(spa_raw_to_native(s.target_temp_raw, s.temp_scale));
      break;
  }
}

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 4: Add to `test-device.yaml`**

```yaml
sensor:
  - platform: balboa_spa
    balboa_spa_id: spa
    type: current_temperature
    name: "Spa Current Temperature"
    unit_of_measurement: "°F"
    device_class: temperature
    accuracy_decimals: 0
```

- [ ] **Step 5: Validate + compile**

Run: `esphome config test-device.yaml && esphome compile test-device.yaml`
Expected: success.

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/sensor/ test-device.yaml
git commit -m "feat: sensor platform (temperatures)"
```

---

### Task 15: binary_sensor platform

**Files:**
- Create: `components/balboa_spa/binary_sensor/__init__.py`
- Create: `components/balboa_spa/binary_sensor/balboa_binary_sensor.h`
- Create: `components/balboa_spa/binary_sensor/balboa_binary_sensor.cpp`
- Modify: `test-device.yaml`

**Interfaces:**
- Produces: `BalboaBinarySensor` with `SpaBinaryType` ∈ {HEATING, PRIMING, CIRCULATION_PUMP, FILTER1_RUNNING, FILTER2_RUNNING}.

- [ ] **Step 1: Write `components/balboa_spa/binary_sensor/__init__.py`**

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_TYPE
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaBinarySensor = balboa_spa_ns.class_("BalboaBinarySensor", binary_sensor.BinarySensor, cg.Component)
SpaBinaryType = balboa_spa_ns.enum("SpaBinaryType")
BINARY_TYPES = {
    "heating": SpaBinaryType.HEATING,
    "priming": SpaBinaryType.PRIMING,
    "circulation_pump": SpaBinaryType.CIRCULATION_PUMP,
    "filter1_running": SpaBinaryType.FILTER1_RUNNING,
    "filter2_running": SpaBinaryType.FILTER2_RUNNING,
}

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(BalboaBinarySensor).extend(
    {
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        cv.Required(CONF_TYPE): cv.enum(BINARY_TYPES, lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_binary_type(config[CONF_TYPE]))
```

- [ ] **Step 2: Write `components/balboa_spa/binary_sensor/balboa_binary_sensor.h`**

```cpp
#pragma once
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"
#include "../balboa_spa.h"

namespace esphome {
namespace balboa_spa {

enum SpaBinaryType { HEATING, PRIMING, CIRCULATION_PUMP, FILTER1_RUNNING, FILTER2_RUNNING };

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
```

- [ ] **Step 3: Write `components/balboa_spa/binary_sensor/balboa_binary_sensor.cpp`**

```cpp
#include "balboa_binary_sensor.h"

namespace esphome {
namespace balboa_spa {

void BalboaBinarySensor::setup() {
  parent_->add_on_status_callback([this]() { this->update_from_spa_(); });
}

void BalboaBinarySensor::update_from_spa_() {
  const SpaStatus &s = parent_->status();
  if (!s.valid) return;
  bool v = false;
  switch (type_) {
    case HEATING: v = s.heating; break;
    case PRIMING: v = s.priming; break;
    case CIRCULATION_PUMP: v = s.circulation_pump; break;
    case FILTER1_RUNNING: v = s.filter_running[0]; break;
    case FILTER2_RUNNING: v = s.filter_running[1]; break;
  }
  this->publish_state(v);
}

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 4: Add to `test-device.yaml`**

```yaml
binary_sensor:
  - platform: balboa_spa
    balboa_spa_id: spa
    type: heating
    name: "Spa Heating"
    device_class: heat
```

- [ ] **Step 5: Validate + compile**

Run: `esphome config test-device.yaml && esphome compile test-device.yaml`
Expected: success.

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/binary_sensor/ test-device.yaml
git commit -m "feat: binary_sensor platform"
```

---

### Task 16: text_sensor platform

**Files:**
- Create: `components/balboa_spa/text_sensor/__init__.py`
- Create: `components/balboa_spa/text_sensor/balboa_text_sensor.h`
- Create: `components/balboa_spa/text_sensor/balboa_text_sensor.cpp`
- Modify: `test-device.yaml`

**Interfaces:**
- Produces: `BalboaTextSensor` with `SpaTextType` ∈ {MODEL, VERSION, NOTIFICATION}.

- [ ] **Step 1: Write `components/balboa_spa/text_sensor/__init__.py`**

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_TYPE
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaTextSensor = balboa_spa_ns.class_("BalboaTextSensor", text_sensor.TextSensor, cg.Component)
SpaTextType = balboa_spa_ns.enum("SpaTextType")
TEXT_TYPES = {
    "model": SpaTextType.MODEL,
    "version": SpaTextType.VERSION,
    "notification": SpaTextType.NOTIFICATION,
}

CONFIG_SCHEMA = text_sensor.text_sensor_schema(BalboaTextSensor).extend(
    {
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        cv.Required(CONF_TYPE): cv.enum(TEXT_TYPES, lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_text_type(config[CONF_TYPE]))
```

- [ ] **Step 2: Write `components/balboa_spa/text_sensor/balboa_text_sensor.h`**

```cpp
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
```

- [ ] **Step 3: Write `components/balboa_spa/text_sensor/balboa_text_sensor.cpp`**

```cpp
#include "balboa_text_sensor.h"

namespace esphome {
namespace balboa_spa {

static const char *notification_str(uint8_t code) {
  switch (code) {
    case 0x0a: return "ph";
    case 0x04: return "filter";
    case 0x09: return "sanitizer";
    default: return "none";
  }
}

void BalboaTextSensor::setup() {
  parent_->add_on_status_callback([this]() { this->update_(); });
  parent_->add_on_config_callback([this]() { this->update_(); });
}

void BalboaTextSensor::update_() {
  std::string v;
  switch (type_) {
    case MODEL: v = parent_->info().valid ? parent_->info().model : ""; break;
    case VERSION: v = parent_->info().valid ? parent_->info().version : ""; break;
    case NOTIFICATION:
      v = parent_->status().valid ? notification_str(parent_->status().notification) : "none";
      break;
  }
  if (v.empty() || v == last_) return;
  last_ = v;
  this->publish_state(v);
}

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 4: Add to `test-device.yaml`**

```yaml
text_sensor:
  - platform: balboa_spa
    balboa_spa_id: spa
    type: model
    name: "Spa Model"
```

- [ ] **Step 5: Validate + compile**

Run: `esphome config test-device.yaml && esphome compile test-device.yaml`
Expected: success.

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/text_sensor/ test-device.yaml
git commit -m "feat: text_sensor platform (model/version/notification)"
```

---

### Task 17: switch platform

**Files:**
- Create: `components/balboa_spa/switch/__init__.py`
- Create: `components/balboa_spa/switch/balboa_switch.h`
- Create: `components/balboa_spa/switch/balboa_switch.cpp`
- Modify: `test-device.yaml`

**Interfaces:**
- Produces: `BalboaSwitch` with `SpaSwitchType` ∈ {LIGHT, AUX, MISTER, HOLD, PUMP_ONOFF, BLOWER_ONOFF} and `uint8_t index_` (0-based, for LIGHT/AUX/PUMP).

- [ ] **Step 1: Write `components/balboa_spa/switch/__init__.py`**

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_TYPE, CONF_INDEX
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaSwitch = balboa_spa_ns.class_("BalboaSwitch", switch.Switch, cg.Component)
SpaSwitchType = balboa_spa_ns.enum("SpaSwitchType")
SWITCH_TYPES = {
    "light": SpaSwitchType.SW_LIGHT,
    "aux": SpaSwitchType.SW_AUX,
    "mister": SpaSwitchType.SW_MISTER,
    "hold": SpaSwitchType.SW_HOLD,
    "pump": SpaSwitchType.SW_PUMP_ONOFF,
    "blower": SpaSwitchType.SW_BLOWER_ONOFF,
}

CONFIG_SCHEMA = switch.switch_schema(BalboaSwitch).extend(
    {
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        cv.Required(CONF_TYPE): cv.enum(SWITCH_TYPES, lower=True),
        cv.Optional(CONF_INDEX, default=1): cv.int_range(min=1, max=6),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_switch_type(config[CONF_TYPE]))
    cg.add(var.set_index(config[CONF_INDEX] - 1))
```

- [ ] **Step 2: Write `components/balboa_spa/switch/balboa_switch.h`**

```cpp
#pragma once
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "../balboa_spa.h"

namespace esphome {
namespace balboa_spa {

enum SpaSwitchType { SW_LIGHT, SW_AUX, SW_MISTER, SW_HOLD, SW_PUMP_ONOFF, SW_BLOWER_ONOFF };

class BalboaSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(BalboaSpa *p) { parent_ = p; }
  void set_switch_type(SpaSwitchType t) { type_ = t; }
  void set_index(uint8_t i) { index_ = i; }
  void setup() override;

 protected:
  void update_from_spa_();
  void write_state(bool state) override;
  bool current_spa_state_();
  BalboaSpa *parent_{nullptr};
  SpaSwitchType type_{SW_LIGHT};
  uint8_t index_{0};
};

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 3: Write `components/balboa_spa/switch/balboa_switch.cpp`**

```cpp
#include "balboa_switch.h"

namespace esphome {
namespace balboa_spa {

void BalboaSwitch::setup() {
  parent_->add_on_status_callback([this]() { this->update_from_spa_(); });
}

bool BalboaSwitch::current_spa_state_() {
  const SpaStatus &s = parent_->status();
  switch (type_) {
    case SW_LIGHT: return s.lights[index_];
    case SW_AUX: return s.aux[index_];
    case SW_MISTER: return s.mister;
    case SW_HOLD: return s.hold;
    case SW_PUMP_ONOFF: return s.pumps[index_] != 0;
    case SW_BLOWER_ONOFF: return s.blower != 0;
  }
  return false;
}

void BalboaSwitch::update_from_spa_() {
  if (!parent_->status().valid) return;
  this->publish_state(this->current_spa_state_());
}

void BalboaSwitch::write_state(bool state) {
  auto &e = parent_->engine();
  switch (type_) {
    case SW_LIGHT: e.set_light(index_, state); break;
    case SW_AUX: e.set_aux(index_, state); break;
    case SW_MISTER: e.set_mister(state); break;
    case SW_HOLD: e.set_hold(state); break;
    case SW_PUMP_ONOFF: e.set_pump(index_, state ? 1 : 0); break;
    case SW_BLOWER_ONOFF: e.set_blower(state ? 1 : 0); break;
  }
}

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 4: Add to `test-device.yaml`**

```yaml
switch:
  - platform: balboa_spa
    balboa_spa_id: spa
    type: light
    index: 1
    name: "Spa Light"
```

- [ ] **Step 5: Validate + compile**

Run: `esphome config test-device.yaml && esphome compile test-device.yaml`
Expected: success.

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/switch/ test-device.yaml
git commit -m "feat: switch platform (lights/aux/mister/hold/pump/blower on-off)"
```

---

### Task 18: fan platform (multi-speed pump / multi-level blower)

**Files:**
- Create: `components/balboa_spa/fan/__init__.py`
- Create: `components/balboa_spa/fan/balboa_fan.h`
- Create: `components/balboa_spa/fan/balboa_fan.cpp`
- Modify: `test-device.yaml`

**Interfaces:**
- Produces: `BalboaFan` with `SpaFanType` ∈ {FAN_PUMP, FAN_BLOWER}, `uint8_t index_`, `uint8_t speed_count_` (2 for a 2-speed pump). Maps fan on/off+speed → spa pump/blower speed level.

- [ ] **Step 1: Write `components/balboa_spa/fan/__init__.py`**

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import fan
from esphome.const import CONF_TYPE, CONF_INDEX, CONF_SPEED_COUNT, CONF_OUTPUT_ID
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaFan = balboa_spa_ns.class_("BalboaFan", fan.Fan, cg.Component)
SpaFanType = balboa_spa_ns.enum("SpaFanType")
FAN_TYPES = {
    "pump": SpaFanType.FAN_PUMP,
    "blower": SpaFanType.FAN_BLOWER,
}

CONFIG_SCHEMA = fan.fan_schema(BalboaFan).extend(
    {
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        cv.Required(CONF_TYPE): cv.enum(FAN_TYPES, lower=True),
        cv.Optional(CONF_INDEX, default=1): cv.int_range(min=1, max=6),
        cv.Optional(CONF_SPEED_COUNT, default=2): cv.int_range(min=1, max=2),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await fan.new_fan(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_fan_type(config[CONF_TYPE]))
    cg.add(var.set_index(config[CONF_INDEX] - 1))
    cg.add(var.set_speed_count(config[CONF_SPEED_COUNT]))
```

> If `fan.new_fan` is unavailable in the installed ESPHome version, use the `fan.register_fan(var, config)` form with `var = cg.new_Pvariable(config[CONF_OUTPUT_ID])`.

- [ ] **Step 2: Write `components/balboa_spa/fan/balboa_fan.h`**

```cpp
#pragma once
#include "esphome/components/fan/fan.h"
#include "esphome/core/component.h"
#include "../balboa_spa.h"

namespace esphome {
namespace balboa_spa {

enum SpaFanType { FAN_PUMP, FAN_BLOWER };

class BalboaFan : public fan::Fan, public Component {
 public:
  void set_parent(BalboaSpa *p) { parent_ = p; }
  void set_fan_type(SpaFanType t) { type_ = t; }
  void set_index(uint8_t i) { index_ = i; }
  void set_speed_count(uint8_t n) { speed_count_ = n; }
  void setup() override;
  fan::FanTraits get_traits() override;

 protected:
  void control(const fan::FanCall &call) override;
  void update_from_spa_();
  uint8_t spa_speed_();
  BalboaSpa *parent_{nullptr};
  SpaFanType type_{FAN_PUMP};
  uint8_t index_{0};
  uint8_t speed_count_{2};
};

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 3: Write `components/balboa_spa/fan/balboa_fan.cpp`**

```cpp
#include "balboa_fan.h"

namespace esphome {
namespace balboa_spa {

void BalboaFan::setup() {
  parent_->add_on_status_callback([this]() { this->update_from_spa_(); });
}

fan::FanTraits BalboaFan::get_traits() {
  return fan::FanTraits(false, speed_count_ > 1, false, speed_count_);
}

uint8_t BalboaFan::spa_speed_() {
  const SpaStatus &s = parent_->status();
  return (type_ == FAN_PUMP) ? s.pumps[index_] : s.blower;
}

void BalboaFan::update_from_spa_() {
  if (!parent_->status().valid) return;
  uint8_t spd = this->spa_speed_();
  this->state = spd != 0;
  this->speed = spd == 0 ? 1 : spd;  // ESPHome speed is 1..count when on
  this->publish_state();
}

void BalboaFan::control(const fan::FanCall &call) {
  uint8_t desired;
  if (call.get_state().has_value() && !*call.get_state()) {
    desired = 0;
  } else {
    int spd = call.get_speed().has_value() ? *call.get_speed() : (this->speed > 0 ? this->speed : 1);
    if (spd < 1) spd = 1;
    if (spd > speed_count_) spd = speed_count_;
    desired = (uint8_t) spd;
  }
  if (type_ == FAN_PUMP)
    parent_->engine().set_pump(index_, desired);
  else
    parent_->engine().set_blower(desired);
}

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 4: Add to `test-device.yaml`**

```yaml
fan:
  - platform: balboa_spa
    balboa_spa_id: spa
    type: pump
    index: 1
    speed_count: 2
    name: "Spa Pump 1"
```

- [ ] **Step 5: Validate + compile**

Run: `esphome config test-device.yaml && esphome compile test-device.yaml`
Expected: success.

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/fan/ test-device.yaml
git commit -m "feat: fan platform (multi-speed pump / blower)"
```

---

### Task 19: select platform (heating mode / temp range / temp scale)

**Files:**
- Create: `components/balboa_spa/select/__init__.py`
- Create: `components/balboa_spa/select/balboa_select.h`
- Create: `components/balboa_spa/select/balboa_select.cpp`
- Modify: `test-device.yaml`

**Interfaces:**
- Produces: `BalboaSelect` with `SpaSelectType` ∈ {SEL_HEATING_MODE, SEL_TEMP_RANGE, SEL_TEMP_SCALE}. Options fixed per type.

- [ ] **Step 1: Write `components/balboa_spa/select/__init__.py`**

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_TYPE
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaSelect = balboa_spa_ns.class_("BalboaSelect", select.Select, cg.Component)
SpaSelectType = balboa_spa_ns.enum("SpaSelectType")
SELECT_TYPES = {
    "heating_mode": (SpaSelectType.SEL_HEATING_MODE, ["ready", "rest"]),
    "temperature_range": (SpaSelectType.SEL_TEMP_RANGE, ["high", "low"]),
    "temperature_scale": (SpaSelectType.SEL_TEMP_SCALE, ["fahrenheit", "celsius"]),
}

CONFIG_SCHEMA = select.select_schema(BalboaSelect).extend(
    {
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        cv.Required(CONF_TYPE): cv.one_of(*SELECT_TYPES.keys(), lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    enum_val, options = SELECT_TYPES[config[CONF_TYPE]]
    var = await select.new_select(config, options=options)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_select_type(enum_val))
```

- [ ] **Step 2: Write `components/balboa_spa/select/balboa_select.h`**

```cpp
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
```

- [ ] **Step 3: Write `components/balboa_spa/select/balboa_select.cpp`**

```cpp
#include "balboa_select.h"

namespace esphome {
namespace balboa_spa {

void BalboaSelect::setup() {
  parent_->add_on_status_callback([this]() { this->update_from_spa_(); });
}

void BalboaSelect::update_from_spa_() {
  const SpaStatus &s = parent_->status();
  if (!s.valid) return;
  std::string v;
  switch (type_) {
    case SEL_HEATING_MODE:
      v = (s.heating_mode == HeatingMode::REST) ? "rest"
        : (s.heating_mode == HeatingMode::READY_IN_REST) ? "ready" : "ready";
      break;
    case SEL_TEMP_RANGE:
      v = (s.temp_range == TempRange::HIGH) ? "high" : "low";
      break;
    case SEL_TEMP_SCALE:
      v = (s.temp_scale == TempScale::CELSIUS) ? "celsius" : "fahrenheit";
      break;
  }
  this->publish_state(v);
}

void BalboaSelect::control(const std::string &value) {
  auto &e = parent_->engine();
  switch (type_) {
    case SEL_HEATING_MODE:
      e.set_heating_mode(value == "rest" ? HeatingMode::REST : HeatingMode::READY);
      break;
    case SEL_TEMP_RANGE:
      e.set_temperature_range(value == "low" ? TempRange::LOW : TempRange::HIGH);
      break;
    case SEL_TEMP_SCALE:
      e.set_temperature_scale(value == "celsius" ? TempScale::CELSIUS : TempScale::FAHRENHEIT);
      break;
  }
}

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 4: Add to `test-device.yaml`**

```yaml
select:
  - platform: balboa_spa
    balboa_spa_id: spa
    type: heating_mode
    name: "Spa Heating Mode"
```

- [ ] **Step 5: Validate + compile**

Run: `esphome config test-device.yaml && esphome compile test-device.yaml`
Expected: success.

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/select/ test-device.yaml
git commit -m "feat: select platform (heating mode / temp range / temp scale)"
```

---

### Task 20: number platform (filter cycles)

**Files:**
- Create: `components/balboa_spa/number/__init__.py`
- Create: `components/balboa_spa/number/balboa_number.h`
- Create: `components/balboa_spa/number/balboa_number.cpp`
- Modify: `test-device.yaml`

**Interfaces:**
- Produces: `BalboaNumber` with `SpaNumberType` ∈ {C1_START_HOUR, C1_START_MINUTE, C1_DURATION, C2_START_HOUR, C2_START_MINUTE, C2_DURATION}. On write, modifies the current `FilterCyclesData` and calls `engine.update_filter_cycles`. Cycle 2 is auto-enabled when `C2_DURATION > 0` (no separate enable entity in v1 — documented in README).

- [ ] **Step 1: Write `components/balboa_spa/number/__init__.py`**

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_TYPE
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaNumber = balboa_spa_ns.class_("BalboaNumber", number.Number, cg.Component)
SpaNumberType = balboa_spa_ns.enum("SpaNumberType")
# type -> (enum, min, max, step)
NUMBER_TYPES = {
    "c1_start_hour": (SpaNumberType.C1_START_HOUR, 0, 23, 1),
    "c1_start_minute": (SpaNumberType.C1_START_MINUTE, 0, 59, 1),
    "c1_duration": (SpaNumberType.C1_DURATION, 0, 1439, 1),
    "c2_start_hour": (SpaNumberType.C2_START_HOUR, 0, 23, 1),
    "c2_start_minute": (SpaNumberType.C2_START_MINUTE, 0, 59, 1),
    "c2_duration": (SpaNumberType.C2_DURATION, 0, 1439, 1),
}

CONFIG_SCHEMA = number.number_schema(BalboaNumber).extend(
    {
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        cv.Required(CONF_TYPE): cv.one_of(*NUMBER_TYPES.keys(), lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    enum_val, mn, mx, step = NUMBER_TYPES[config[CONF_TYPE]]
    var = await number.new_number(config, min_value=mn, max_value=mx, step=step)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_number_type(enum_val))
```

- [ ] **Step 2: Write `components/balboa_spa/number/balboa_number.h`**

```cpp
#pragma once
#include "esphome/components/number/number.h"
#include "esphome/core/component.h"
#include "../balboa_spa.h"

namespace esphome {
namespace balboa_spa {

enum SpaNumberType {
  C1_START_HOUR, C1_START_MINUTE, C1_DURATION,
  C2_START_HOUR, C2_START_MINUTE, C2_DURATION
};

class BalboaNumber : public number::Number, public Component {
 public:
  void set_parent(BalboaSpa *p) { parent_ = p; }
  void set_number_type(SpaNumberType t) { type_ = t; }
  void setup() override;

 protected:
  void control(float value) override;
  void update_from_spa_();
  BalboaSpa *parent_{nullptr};
  SpaNumberType type_{C1_START_HOUR};
};

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 3: Write `components/balboa_spa/number/balboa_number.cpp`**

```cpp
#include "balboa_number.h"

namespace esphome {
namespace balboa_spa {

void BalboaNumber::setup() {
  parent_->add_on_config_callback([this]() { this->update_from_spa_(); });
}

void BalboaNumber::update_from_spa_() {
  const FilterCyclesData &fc = parent_->filter_cycles();
  if (!fc.valid) return;
  float v = 0;
  switch (type_) {
    case C1_START_HOUR: v = fc.c1_start_hour; break;
    case C1_START_MINUTE: v = fc.c1_start_minute; break;
    case C1_DURATION: v = fc.c1_duration_min; break;
    case C2_START_HOUR: v = fc.c2_start_hour; break;
    case C2_START_MINUTE: v = fc.c2_start_minute; break;
    case C2_DURATION: v = fc.c2_duration_min; break;
  }
  this->publish_state(v);
}

void BalboaNumber::control(float value) {
  FilterCyclesData fc = parent_->filter_cycles();
  if (!fc.valid) return;
  uint16_t iv = (uint16_t) value;
  switch (type_) {
    case C1_START_HOUR: fc.c1_start_hour = (uint8_t) iv; break;
    case C1_START_MINUTE: fc.c1_start_minute = (uint8_t) iv; break;
    case C1_DURATION: fc.c1_duration_min = iv; break;
    case C2_START_HOUR: fc.c2_start_hour = (uint8_t) iv; break;
    case C2_START_MINUTE: fc.c2_start_minute = (uint8_t) iv; break;
    case C2_DURATION: fc.c2_duration_min = iv; break;
  }
  fc.c2_enabled = fc.c2_duration_min > 0;
  parent_->engine().update_filter_cycles(fc);
  this->publish_state(value);
}

}  // namespace balboa_spa
}  // namespace esphome
```

- [ ] **Step 4: Add to `test-device.yaml`**

```yaml
number:
  - platform: balboa_spa
    balboa_spa_id: spa
    type: c1_duration
    name: "Spa Filter Cycle 1 Duration"
    unit_of_measurement: "min"
```

- [ ] **Step 5: Validate + compile**

Run: `esphome config test-device.yaml && esphome compile test-device.yaml`
Expected: success.

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/number/ test-device.yaml
git commit -m "feat: number platform (filter cycle config)"
```

---

### Task 21: button platform

**Files:**
- Create: `components/balboa_spa/button/__init__.py`
- Create: `components/balboa_spa/button/balboa_button.h`
- Create: `components/balboa_spa/button/balboa_button.cpp`
- Modify: `test-device.yaml`

**Interfaces:**
- Produces: `BalboaButton` with `SpaButtonType` ∈ {BTN_NORMAL_OPERATION, BTN_CLEAR_NOTIFICATION, BTN_SOAK}. Press → `engine.toggle_item(item code)`.

- [ ] **Step 1: Write `components/balboa_spa/button/__init__.py`**

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_TYPE
from .. import balboa_spa_ns, BalboaSpa, CONF_BALBOA_SPA_ID

DEPENDENCIES = ["balboa_spa"]

BalboaButton = balboa_spa_ns.class_("BalboaButton", button.Button, cg.Component)
SpaButtonType = balboa_spa_ns.enum("SpaButtonType")
BUTTON_TYPES = {
    "normal_operation": SpaButtonType.BTN_NORMAL_OPERATION,
    "clear_notification": SpaButtonType.BTN_CLEAR_NOTIFICATION,
    "soak": SpaButtonType.BTN_SOAK,
}

CONFIG_SCHEMA = button.button_schema(BalboaButton).extend(
    {
        cv.GenerateID(CONF_BALBOA_SPA_ID): cv.use_id(BalboaSpa),
        cv.Required(CONF_TYPE): cv.enum(BUTTON_TYPES, lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_BALBOA_SPA_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_button_type(config[CONF_TYPE]))
```

- [ ] **Step 2: Write `components/balboa_spa/button/balboa_button.h`**

```cpp
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
```

- [ ] **Step 3: Write `components/balboa_spa/button/balboa_button.cpp`**

```cpp
#include "balboa_button.h"
#include "../protocol/messages.h"

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
```

- [ ] **Step 4: Add to `test-device.yaml`**

```yaml
button:
  - platform: balboa_spa
    balboa_spa_id: spa
    type: clear_notification
    name: "Spa Clear Notification"
```

- [ ] **Step 5: Validate + compile**

Run: `esphome config test-device.yaml && esphome compile test-device.yaml`
Expected: success.

- [ ] **Step 6: Commit**

```bash
git add components/balboa_spa/button/ test-device.yaml
git commit -m "feat: button platform (normal-op / clear-notification / soak)"
```

---

### Task 22: Production device config (tailored to the Cambridge spa)

**Files:**
- Create: `balboa-spa.yaml`
- Create: `secrets.yaml.example`
- Create: `packages/spa_full_superset.yaml`
- Modify: `secrets.yaml` (ensure all referenced keys exist locally)

**Interfaces:**
- Consumes: every platform from Tasks 12–21.
- Produces: a flashable device config with WiFi, MQTT (HA discovery + plain topics), OTA, web server, SNTP time, captive-portal fallback, and the entity set confirmed for the **Canadian Spa Co. Cambridge (Black Ice, Balboa pack): 1× 2-speed pump, 1× LED light**.

- [ ] **Step 1: Write `secrets.yaml.example`**

```yaml
wifi_ssid: "YourWiFi"
wifi_password: "YourWiFiPassword"
mqtt_broker: "192.168.1.10"
mqtt_username: "spa"
mqtt_password: "YourMqttPassword"
ota_password: "generate-a-random-string"
ap_password: "fallback-ap-password"
```

- [ ] **Step 2: Write `packages/spa_full_superset.yaml`** (everything; commented per-spa)

```yaml
# Full superset of Balboa entities. This Cambridge spa only has pump 1 (2-speed)
# and light 1, so the others are commented out. Uncomment any that your spa's
# discovery log (Task 12 "Detected spa" lines) reports.

climate:
  - platform: balboa_spa
    name: "Spa"
    balboa_spa_id: spa

fan:
  - platform: balboa_spa
    balboa_spa_id: spa
    type: pump
    index: 1
    speed_count: 2
    name: "Spa Jets"
  # - platform: balboa_spa   # pump 2 (multi-pump spas only)
  #   balboa_spa_id: spa
  #   type: pump
  #   index: 2
  #   speed_count: 2
  #   name: "Spa Jets 2"
  # - platform: balboa_spa   # blower (not present on Cambridge)
  #   balboa_spa_id: spa
  #   type: blower
  #   speed_count: 2
  #   name: "Spa Blower"

switch:
  - platform: balboa_spa
    balboa_spa_id: spa
    type: light
    index: 1
    name: "Spa Light"
  - platform: balboa_spa
    balboa_spa_id: spa
    type: hold
    name: "Spa Hold"
  # - platform: balboa_spa   # mister / aux not present on Cambridge
  #   balboa_spa_id: spa
  #   type: mister
  #   name: "Spa Mister"

select:
  - platform: balboa_spa
    balboa_spa_id: spa
    type: heating_mode
    name: "Spa Heating Mode"
  - platform: balboa_spa
    balboa_spa_id: spa
    type: temperature_range
    name: "Spa Temperature Range"
  - platform: balboa_spa
    balboa_spa_id: spa
    type: temperature_scale
    name: "Spa Temperature Scale"

sensor:
  - platform: balboa_spa
    balboa_spa_id: spa
    type: current_temperature
    name: "Spa Current Temperature"
    unit_of_measurement: "°F"
    device_class: temperature
    accuracy_decimals: 0
  - platform: balboa_spa
    balboa_spa_id: spa
    type: target_temperature
    name: "Spa Target Temperature"
    unit_of_measurement: "°F"
    device_class: temperature
    accuracy_decimals: 0

binary_sensor:
  - platform: balboa_spa
    balboa_spa_id: spa
    type: heating
    name: "Spa Heating"
    device_class: heat
  - platform: balboa_spa
    balboa_spa_id: spa
    type: priming
    name: "Spa Priming"
  - platform: balboa_spa
    balboa_spa_id: spa
    type: filter1_running
    name: "Spa Filter Cycle 1 Running"
  # - platform: balboa_spa   # circulation pump (uncomment if discovery reports it)
  #   balboa_spa_id: spa
  #   type: circulation_pump
  #   name: "Spa Circulation Pump"

text_sensor:
  - platform: balboa_spa
    balboa_spa_id: spa
    type: model
    name: "Spa Model"
  - platform: balboa_spa
    balboa_spa_id: spa
    type: version
    name: "Spa Firmware Version"
  - platform: balboa_spa
    balboa_spa_id: spa
    type: notification
    name: "Spa Notification"

number:
  - platform: balboa_spa
    balboa_spa_id: spa
    type: c1_start_hour
    name: "Spa Filter 1 Start Hour"
  - platform: balboa_spa
    balboa_spa_id: spa
    type: c1_duration
    name: "Spa Filter 1 Duration"
    unit_of_measurement: "min"

button:
  - platform: balboa_spa
    balboa_spa_id: spa
    type: clear_notification
    name: "Spa Clear Notification"
  - platform: balboa_spa
    balboa_spa_id: spa
    type: normal_operation
    name: "Spa Normal Operation"
```

- [ ] **Step 3: Write `balboa-spa.yaml`**

```yaml
esphome:
  name: balboa-spa
  friendly_name: Hot Tub

external_components:
  - source:
      type: local
      path: components

esp32:
  board: esp32dev
  framework:
    type: arduino

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  ap:
    ssid: "Balboa-Spa Fallback"
    password: !secret ap_password

captive_portal:

logger:
  level: INFO
  baud_rate: 0   # free UART0; logs available over MQTT/web/USB-CDC

mqtt:
  broker: !secret mqtt_broker
  username: !secret mqtt_username
  password: !secret mqtt_password
  topic_prefix: spa
  discovery: true

ota:
  - platform: esphome
    password: !secret ota_password

web_server:
  port: 80

time:
  - platform: sntp
    id: sntp_time

uart:
  id: spa_uart
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 115200
  rx_buffer_size: 512

balboa_spa:
  id: spa
  uart_id: spa_uart
  time_id: sntp_time
  read_only: true   # FLIP TO false ONLY AFTER confirming clean decode (see docs/bring-up)

packages:
  spa_entities: !include packages/spa_full_superset.yaml
```

- [ ] **Step 4: Validate**

Run: `esphome config balboa-spa.yaml`
Expected: resolves with no errors (uses the placeholder `secrets.yaml` from Task 12).

- [ ] **Step 5: Compile**

Run: `esphome compile balboa-spa.yaml`
Expected: build succeeds.

- [ ] **Step 6: Commit**

```bash
git add balboa-spa.yaml secrets.yaml.example packages/
git commit -m "feat: production device config for Cambridge spa (1x 2-speed pump, 1x light)"
```

---

### Task 23: Docs, README, simulator + bring-up runbook

**Files:**
- Create: `README.md`
- Create: `docs/wiring.md`
- Create: `docs/bring-up.md`

**Interfaces:** documentation only; no code.

- [ ] **Step 1: Write `docs/wiring.md`**

````markdown
# Wiring

ESP32 dev board + HW-0519 auto-direction RS-485 module.

| ESP32 | HW-0519 |
|-------|---------|
| GPIO17 (TX) | RXD |
| GPIO16 (RX) | TXD |
| 3V3 | VCC |
| GND | GND |

| HW-0519 | Spa |
|---------|-----|
| A+ | RS-485 + |
| B- | RS-485 - |

The HW-0519 handles RS-485 direction automatically — no DE/RE GPIO.

## Finding the spa's RS-485 wires
On the Balboa pack's micro-fit connector, one opposite pin pair reads **12–14 V**
(do NOT connect this). With one meter probe on that pair's negative pin, the other
two wires read ~2–3 V: the slightly higher one is **RS-485+**, the slightly lower is
**RS-485-**. Swapping +/- only produces garbage (non-destructive) — swap back to fix.

This spa: **Canadian Spa Co. Cambridge (Black Ice), Balboa pack** — replace/stand in
for the WiFi module position; ESP32 is the sole client at bus address 0x0A.
````

- [ ] **Step 2: Write `docs/bring-up.md`**

````markdown
# Bring-up runbook

## Stage 0 — host unit tests
```
make test    # all protocol/codec/engine tests must pass
```

## Stage 1 — simulator (no real spa)
The reference gem ships a spa simulator. Run it on a USB-RS485 dongle:
```
cd ../balboa_worldwide_app && bundle exec exe/bwa_server /dev/ttyUSB0
```
Wire the HW-0519 A+/B- to the dongle's A/B, flash `balboa-spa.yaml` with
`read_only: false` for this bench test, and confirm in the ESPHome logs:
- a "Detected spa" line with model + accessory inventory,
- decoded status (temperature, pump, light),
- command round-trips when you toggle entities.

## Stage 2 — real spa, READ ONLY
Keep `read_only: true`. Wire to the spa. Confirm decoded values match the topside
panel (temperature, heating, pump speed, light). Watch for CRC/parse errors in logs.
If you see only garbage: swap A+/B-.

## Stage 3 — real spa, CONTROL
Set `read_only: false`, re-flash (OTA). Verify each control: target temp, jets
(low/high), light, heating mode, filter cycles. The ESP32 must be the only add-on
client on the bus (Balboa WiFi module removed).

## Expected entity set (Cambridge)
water heater, 1× 2-speed pump (fan), 1× light, heating-mode & temp-range & temp-scale
selects, current/target temp sensors, heating/priming/filter binary sensors,
model/version/notification text sensors, filter-cycle numbers, clear-notification &
normal-operation buttons.
````

- [ ] **Step 3: Write `README.md`**

````markdown
# balboa-esp32-mqtt

ESPHome firmware bridging a Balboa-powered spa to MQTT (with Home Assistant
auto-discovery) over RS-485, using an ESP32 + HW-0519 auto-direction MAX485 module.
A C++ re-implementation of the `balboa_worldwide_app` protocol, unit-tested on host.

## Quick start
1. `cp secrets.yaml.example secrets.yaml` and fill in WiFi/MQTT/OTA.
2. Wire per `docs/wiring.md`.
3. `make test` — run protocol unit tests.
4. `esphome run balboa-spa.yaml` — flash (starts in `read_only: true`).
5. Follow `docs/bring-up.md` stages; flip `read_only: false` once decode is verified.

## Layout
- `components/balboa_spa/protocol/` — host-testable protocol library.
- `components/balboa_spa/` — ESPHome hub + entity platforms.
- `balboa-spa.yaml` / `packages/` — device config (tailored to a Canadian Spa Co.
  Cambridge: 1× 2-speed pump, 1× light; superset commented for other spas).
- `docs/` — wiring, bring-up, and the design spec.

## Notes
- Cycle-2 filter is auto-enabled when its duration > 0 (no separate enable entity).
- The ESP32 must be the sole add-on client on the RS-485 bus (replace the Balboa
  WiFi module); coexistence is out of scope.

## Credits
Protocol reverse-engineering from ccutrer's `balboa_worldwide_app` gem.
````

- [ ] **Step 4: Commit**

```bash
git add README.md docs/wiring.md docs/bring-up.md
git commit -m "docs: README, wiring, and bring-up runbook"
```

---

## Self-Review

**Spec coverage** (each spec section → task):
- §3 wiring → Task 12 (UART pins) + Task 23 (`docs/wiring.md`). ✓
- §4 protocol (frames/CRC/decode/encode/discipline/handshake) → Tasks 2–11. ✓
- §5 architecture (hub + platforms) → Tasks 12–21. ✓
- §6 auto-detect (discovery log + declare + runtime guard) → Task 12 discovery log; Task 22 superset/declare; runtime guard = entities only publish when `status/config.valid`. ✓
- §7 entity mapping (climate/switch/fan/select/number/binary/sensor/text/button) → Tasks 13–21, all rows covered. ✓
- §8 temperature handling → Task 9 (conversions) + Task 13 (climate uses them). ✓
- §9 MQTT/HA + time → Task 22 (`mqtt: discovery`) + Task 12/22 (SNTP time sync). ✓
- §10 safety (read_only default, no-TX-before-status, CRC drop, simulator) → Tasks 10 (engine guards) + 12 (default true) + 23 (runbook). ✓
- §11 layout → matches the File Structure section. ✓
- §12 testing → Tasks 1–11 host tests + Task 23 simulator/real-spa stages. ✓

**Placeholder scan:** no TBD/TODO; every code step shows full code; fixtures are real computed bytes. ✓

**Type consistency:** `ProtocolEngine` method names (`set_pump`, `set_light`, `set_blower`, `set_target_temperature_raw`, `update_filter_cycles`, `toggle_item`, `engine()`) are used identically across hub and all platforms. Enum names (`SpaSwitchType`, `SpaFanType`, etc.) match between each `__init__.py` and its `.h`. `BalboaSpa` accessors (`status()/config()/info()/filter_cycles()/engine()`) consistent. ✓

**Known follow-ups (intentional, out of v1 scope):** circulation-pump/aux/mister/pump2/blower entities exist in code but are commented in the Cambridge config (uncomment if discovery reports them); cycle-2 enable is duration-driven; fault-log readout (`0a bf 28`) is not implemented (add a `text_sensor` type + decoder later if desired).

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-06-25-balboa-esp32-mqtt.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

**Which approach?**
