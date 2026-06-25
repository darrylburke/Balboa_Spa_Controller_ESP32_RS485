# Balboa ESP32/RS-485 → MQTT Gateway — Design

**Date:** 2026-06-25
**Status:** Approved (brainstorming complete; next step: implementation plan)
**Reference implementation:** `../balboa_worldwide_app` (ccutrer's `balboa_worldwide_app` Ruby gem) — this project is a faithful C++/ESPHome re-implementation of its protocol logic.

## 1. Goal

A standalone ESPHome firmware for an **ESP32 + HW-0519 (auto-direction MAX485) module** that:

- Acts as the **sole RS-485 client** on a Balboa spa bus (replacing/standing in for the Balboa Wi-Fi module), at bus address `0x0A`.
- Decodes the full spa state and controls the spa.
- Bridges everything to the user's **MQTT broker** with **Home Assistant auto-discovery** plus human-readable plain topics.

It is a re-implementation of the `balboa_worldwide_app` protocol, not a wrapper around it.

## 2. Locked decisions

| Decision | Choice |
|---|---|
| Firmware framework | **ESPHome external component** (handles WiFi/OTA/MQTT/web/logging) |
| Feature scope | **Full parity**, auto-detected from the spa's configuration message |
| RS-485 hardware | **HW-0519 auto-direction module** — no DE/RE GPIO, no flow-control pin in firmware |
| UART | ESP32 **UART2**, `GPIO17 = TX`, `GPIO16 = RX`, **115200 8N1** (USB UART0 left free for logs) |
| Bus role | ESP32 is the **sole client at 0x0A** (Wi-Fi module removed / not present) |
| MQTT style | **HA auto-discovery + plain topics**, `topic_prefix: spa` |
| Multi-speed pumps/blower | `fan` entities (speed); single-speed → on/off `switch` |
| Repo name | `balboa-esp32-mqtt` (sibling to the gem) |

## 2a. Confirmed target spa (added after manual review)

The specific spa is a **Canadian Spa Co. Cambridge, "Black Ice" Premium 240V/60Hz** (owner's manual KM-10224, at `/home/darrylb/Mega/cottage/1940 cunnington/Spa/`). The manual's wiring diagram shows the control pack stamped **"POWERED BY BALBOA"**, and the topside startup screen reads **`M 100 V 35 _201`** (Balboa software ID `M100`) — confirming this RS-485 protocol applies. Confirmed accessory inventory:

- **1 × 2-speed pump** (5 HP) — topside "JETS 1" low/high. (Topside notes "JETS 2 — *only on multi-pump spas*"; this spa has no pump 2.)
- **LED perimeter lighting + LED cup holders** → 1 controllable light.
- **No electric blower** (air/waterfall/diverter are manual valves), **no mister/aux** (aromatherapy is a passive canister; ozone Glacier AO₃P runs automatically).
- Temperature range 80–104 °F; Ready/Rest heating modes; configurable filter cycles.

So the concrete entity set is: water heater, 1× 2-speed pump (`fan`), 1× light (`switch`), heating-mode/temp-range/temp-scale (`select`), current/target temp (`sensor`), heating/priming/filter-running (`binary_sensor`), model/version/notification (`text_sensor`), filter-cycle (`number`), clear-notification/normal-operation (`button`). The full superset remains in the component (commented in YAML) for portability and in case discovery reports more (e.g. a circulation pump).

## 3. Hardware & wiring

ESP32 dev board (WROOM-32, CP2102 USB — currently flashed with Espressif AT firmware, to be overwritten by ESPHome) + separate HW-0519 auto-direction RS-485 module.

```
ESP32 GPIO17 (TX) ──→ RXD   HW-0519
ESP32 GPIO16 (RX) ←── TXD   HW-0519
ESP32 3V3 ─────────→ VCC
ESP32 GND ─────────→ GND
HW-0519 A+ ────────→ spa RS-485 +
HW-0519 B− ────────→ spa RS-485 −
```

- HW-0519 has an onboard 120 Ω termination resistor and 3.3 V/5 V-safe signalling, so it can be powered from and directly interface the ESP32 at 3.3 V.
- Identifying the spa's RS-485 +/− wires: per the gem README, one opposite pin pair on the spa's micro-fit connector reads 12–14 V (do **not** connect this); keep one probe on the negative end and the remaining two wires read ~2–3 V — the slightly higher is RS-485+, slightly lower is RS-485−. Swapping +/− only yields garbage (non-destructive); swap back to fix.

## 4. Protocol reference (for the implementer)

All multi-byte framing ported 1:1 from the gem's `lib/bwa/message.rb`, `lib/bwa/crc.rb`, and `lib/bwa/messages/*`.

### 4.1 Frame format
```
0x7E | LEN | SRC | MT0 MT1 [MT2...] | payload... | CRC | 0x7E
```
- Start/end byte: `0x7E` (`~`).
- `LEN` = bytes from `SRC` through `CRC` inclusive (i.e. `payload.len + 5`).
- `CRC` = **CRC-8**, polynomial per `Digest::CRC8`, **initial value `0x02`, final XOR `0x02`**, computed over bytes `[LEN .. last payload byte]` (everything between the framing `0x7E`s except the CRC itself).
- RS-485 line settings: **115200, 8, N, 1**.

### 4.2 Messages to DECODE (incoming)
| Name | Type bytes | Notes |
|---|---|---|
| Status | `FF AF 13` | ~1/sec. Length 23–32 (grows in later firmware). Source of live state. |
| Ready | `10 BF 06` | **Send window** — transmit exactly one queued frame immediately after this. |
| New Client Clear To Send | `10 BF 00` | New-client announce window. |
| Control Configuration | `0A BF 24` | Model (ASCII, bytes 4–11, stripped) + version (`V{b2}.{b3}`). |
| Control Configuration 2 | `0A BF 2E` | **Accessory inventory** → auto-detect (see 4.5). |
| Filter Cycles | `0A BF 23` | Two cycles: start hour/min, duration (h*60+m); cycle-2 high bit of start-hour = enabled. |

### 4.3 Status decode (`FF AF 13` payload, byte offsets)
- `[0]` flags: `0x05` → Hold.
- `[1]`: `0x01` → Priming; `0x03` → notification present (code in `[6]`).
- `[2]` Current temp (`0xFF` = unknown; halve if Celsius).
- `[3]` Hour, `[4]` Minute.
- `[5]` flags: `&0x03` heating mode (0 ready, 1 rest, 2 ready_in_rest).
- `[6]` notification code: `0x00`→none, `0x04`→filter, `0x09`→sanitizer, `0x0A`→ph.
- `[9]` flags: `0x01` Celsius, `0x02` 24-hour time, `0x04`/`0x08` filter cycle 1/2 running.
- `[10]` flags: `0x30` heating active, `0x04` temp range high(1)/low(0).
- `[11]` pumps: `pump0 = &0x03`, `pump1 = (>>2)&0x03`, `pump2 = (>>4)&0x03`, `pump3 = (>>6)&0x03`.
- `[12]` pumps: `pump4 = &0x03`, `pump5 = (>>2)&0x03`.
- `[13]` flags: `0x02` circulation pump, blower `= (>>2)&0x03`.
- `[14]` lights: `light0 = &0x03 != 0`, `light1 = (>>2)&0x03 != 0`.
- `[15]` flags: `0x01` mister, `0x08` aux0, `0x10` aux1.
- `[20]` Target temp (halve if Celsius).

### 4.4 Messages to ENCODE (outgoing, `SRC = 0x0A`)
| Name | Type bytes | Payload |
|---|---|---|
| Toggle Item | `0A BF 11` | `[item, 0x00]` (item codes below) |
| Set Target Temperature | `0A BF 20` | `[temp]` (doubled if Celsius) |
| Set Time | `0A BF 21` | `[hour (high bit = 24h), minute]` |
| Set Temperature Scale | `0A BF 27` | `[0x01, scale]` (0=F, 1=C) |
| Control Configuration Request | `0A BF 22` | type1=`02 00 00` (info), type2=`00 00 01` (config2), type3=`01 00 00` (filter cycles) |
| Filter Cycles (write) | `0A BF 23` | c1 h/m, c1 dur h/m, c2 start-hour (\|0x80 if enabled), c2 min, c2 dur h/m |

### 4.5 Toggle Item codes (`0A BF 11`)
```
normal_operation 0x01   clear_notification 0x03
pump1 0x04  pump2 0x05  pump3 0x06  pump4 0x07  pump5 0x08  pump6 0x09
blower 0x0C  mister 0x0E  light1 0x11  light2 0x12  aux1 0x16  aux2 0x17
soak 0x1D  hold 0x3C  temperature_range 0x50  heating_mode 0x51
```

### 4.6 Accessory inventory decode (`0A BF 2E` — Control Configuration 2)
- `[0]`: pump0 `&0x03`, pump1 `(>>2)&0x03`, pump2 `(>>4)&0x03`, pump3 `(>>6)&0x03` (each = max speed 0/1/2).
- `[1]`: pump4 `&0x03`, pump5 `(>>6)&0x03`.
- `[2]`: light0 `&0x03 != 0`, light1 `(>>6)&0x03 != 0`.
- `[3]`: blower `&0x03`, circulation pump `(>>6)&0x03 != 0`.
- `[4]`: mister `&0x30 != 0`, aux0 `&0x01`, aux1 `&0x02`.

### 4.7 Bus send discipline (critical)
RS-485 is shared half-duplex. The firmware MUST:
1. Maintain a small command FIFO.
2. Transmit **exactly one** queued frame only in the instant immediately after receiving a `Ready` (`10 BF 06`) addressed to `0x0A`.
3. Represent multi-step actions (e.g. pump 0→2 = two `toggle`s; heating-mode transitions = 1–2 toggles; blower levels) as **N enqueued toggles drained one-per-Ready** — same as the gem's queue model.
4. Never transmit before (a) at least one valid `Status` has been received (proves wiring/baud/CRC) and (b) a `Ready` window is seen.

### 4.8 Initial handshake (port of `bwa_mqtt_bridge` startup)
On boot, request `Configuration`, `ControlConfiguration` (info), `ControlConfiguration2` (config2), and `FilterCycles` until a full configuration is held; then publish entities/state. Requests are themselves queued and sent on `Ready`.

## 5. Architecture

```
ESP32 (ESPHome)
  balboa_spa hub component  (Component + uart::UARTDevice on UART2)
    • frame scanner + CRC-8(init/xor 0x02)
    • decode → live SpaState struct
    • command FIFO, drained 1-per-Ready
    • bus join: act as 0x0A, request config, TX only on Ready
        ▲ state            │ enqueue command
    entity platforms: climate, switch, fan, select, number,
                      binary_sensor, sensor, text_sensor, button
  wifi · mqtt(discovery) · ota · web_server · time(sntp) · captive_portal
        │
   UART2 17/16 → HW-0519 → A+/B− → spa RS-485 bus
        │
   WiFi → MQTT broker → Home Assistant
```

The **hub** owns all protocol I/O and state. Each **entity platform** holds a pointer to the hub, reads state for its publish value, and enqueues commands on control. This yields per-entity MQTT topics + HA discovery automatically.

Component layout follows ESPHome conventions (hub `__init__.py` + `balboa_spa.{h,cpp}`, plus a subdirectory per entity platform with its own `__init__.py` schema and C++ class).

## 6. Auto-detect within ESPHome's compile-time entities

ESPHome entities are declared at build time, so "auto-detect" is a two-phase flow:

- **Phase A — discovery:** flash a minimal config; the hub logs the decoded `ControlConfiguration2` (pumps present + their max speeds, # lights, blower levels, mister, aux, circ pump) and the model string.
- **Phase B — declare:** the user enables matching entities from a provided **superset template** (`packages/`, everything present but commented; uncomment what the log reported).
- **Runtime guard:** the hub marks any accessory the spa does not report as `unavailable`, so even an over-declared config shows no ghost controls.

## 7. Entity mapping (full parity)

| Spa feature | ESPHome entity | Control path |
|---|---|---|
| Current/target temp + heating | `climate` (water heater; off/heat) | `SetTargetTemperature`; read heating flag |
| Heating mode (ready/rest/ready_in_rest) | `select` | `toggle heating_mode` (1–2 toggles) |
| Temperature range (high/low) | `select` | `toggle temperature_range` |
| Pump N (max speed 1) | `switch` | `toggle pumpN` |
| Pump N (max speed 2) | `fan` (speed) | N `toggle pumpN` to reach speed |
| Blower (1 or 2 levels) | `switch` / `fan` | `toggle blower` |
| Light 1/2, Aux 1/2, Mister | `switch` | `toggle lightN`/`auxN`/`mister` |
| Hold | `switch` | `toggle hold` |
| Circulation pump, Priming, Heating, Filter-cycle running | `binary_sensor` | read-only |
| Notification, Model/version, Fault | `text_sensor` | read-only |
| Filter cycle 1/2 start hour, start minute, duration | `number` | `FilterCycles` write |
| Filter cycle 2 enabled | `switch` | `FilterCycles` write |
| Normal-op / Clear-notification / Soak | `button` | `toggle normal_operation`/`clear_notification`/`soak` |

## 8. Temperature handling

The spa reports °F (integer) or °C (half-degree). The `climate` entity uses ESPHome's internal Celsius convention, so conversions are centralized in one helper (spa-units → °C for publishing, HA °C → spa-units for `SetTargetTemperature`) and unit-tested. Raw spa-unit values are also exposed on plain `sensor`s. A `temperature-scale` `select` lets the user change the spa's own scale (`SetTemperatureScale`). Target temp ranges follow the gem: high range 80–104 °F / 26–40 °C; low range 50–80 °F / 10–26 °C.

## 9. MQTT / HA & time

- `mqtt:` → user's broker (host/user/pass in `secrets.yaml`), `discovery: true`, `topic_prefix: spa`. Produces a "BWA Link"-style HA device automatically **and** readable topics (e.g. `spa/climate/spa_water_heater/state`).
- `time:` SNTP; the hub syncs the spa clock via `SetTime` when drift > 1 minute (port of the bridge logic), respecting the spa's 12/24-hour flag.
- Native ESPHome `api:` left optional (can run alongside MQTT).
- WiFi/MQTT provisioning via compile-time `secrets.yaml`; `captive_portal` + WiFi AP fallback for first setup; OTA enabled.

## 10. Safety / bring-up guardrails

- **`read_only` YAML flag, default ON** for the first flash — decode only, never transmit.
- No TX until ≥1 valid `Status` received **and** a `Ready` to `0x0A` is seen.
- Every frame CRC-validated; malformed frames silently dropped and resynced on next `0x7E`.
- Bench-test against the gem's **`bwa_server` spa simulator** (`exe/bwa_server`) before connecting real hardware.

## 11. Project layout

```
balboa-esp32-mqtt/
  components/balboa_spa/        # hub + per-platform C++/py
    __init__.py, balboa_spa.{h,cpp}
    climate/  switch/  fan/  select/  number/
    binary_sensor/  sensor/  text_sensor/  button/
  balboa-spa.yaml              # main device config
  packages/                    # entity groups, uncomment per detected spa
  secrets.yaml.example
  tests/                       # host-build codec tests + captured-frame fixtures
  docs/                        # wiring, bring-up runbook, protocol notes
  README.md
```

## 12. Testing strategy

1. **Off-device unit tests (TDD)** for the codec: framing, CRC, decode, encode — using the documented example frames and any captures as fixtures. The codec is written platform-agnostic so it builds on host.
2. **Simulator integration:** ESP32 (or host build) ↔ `bwa_server` fake spa over the HW-0519 + a USB-RS485 dongle; verify handshake, config auto-detect, decode, and command round-trips.
3. **Real spa, read-only:** `read_only: true`; confirm decoded state matches the topside panel.
4. **Real spa, control:** flip `read_only` off; verify each command (temp, pumps, lights, modes, filter cycles).

## 13. Key risks

- **Ready-window timing:** ESPHome's cooperative `loop()` can be delayed by WiFi/MQTT and miss the sub-millisecond send window. Mitigation: do read→parse→immediate-TX synchronously in `loop()` with minimal per-iteration work; the auto-direction module removes all DE/RE timing concern; if windows are still missed, escalate to tighter UART servicing. **Validate this early** (simulator stage).
- **Compile-time entities vs. true auto-detect:** mitigated by discovery-log → declare flow + runtime `unavailable` guarding.
- **Temp scale/unit conversions:** centralized and unit-tested.

## 14. Out of scope (first version)

- Coexistence with a still-connected Balboa Wi-Fi module (two clients on the bus). Designed for sole-client only.
- Wi-Fi settings push to the spa (`0A BF 92`) — not needed when the ESP32 is the gateway.
- Non-ESP32 targets.

## 15. Open questions

None blocking. Exact GPIO assignments are configurable in YAML; the discovery pass finalizes which entities to declare for the specific spa.
