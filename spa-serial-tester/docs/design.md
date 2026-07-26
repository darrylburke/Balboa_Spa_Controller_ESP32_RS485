# Spa Serial Tester — Design

**Date:** 2026-07-03
**Purpose:** A Node.js bench tool to read/monitor/confirm the Balboa RS-485 protocol over a local USB adapter, before committing to the ESP32 gateway. Guided flow: **read → confirm model → perform action.**

## Hardware
- **Waveshare Industrial USB-to-RS485** (FT232RL + SP485EEN) on `/dev/ttyUSB0`, 115200 8N1.
- **Auto-direction** transceiver (hardware TX/RX switching), so both **reading and sending work directly** from Node — the balboa gem's "MAX485 not supported on Linux" caveat does not apply.
- Wire the adapter's `A`/`B` to the spa's RS-485 ± (a Y-cable tap, or the WiFi-module port). If only garbage decodes, swap A/B.

## Components (small, focused)
- **`protocol.js`** — pure JS port of the Balboa protocol (no hardware deps):
  - `crc8(bytes)` — CRC-8, poly `0x07`, init `0x02`, final XOR `0x02`, no reflection.
  - `scanFrame(buf)` — find/validate one `~`-framed message (`0x7E LEN SRC T0 T1 payload… CRC 0x7E`; `LEN = payloadLen+5`; CRC over `LEN…lastPayload`). Returns `{frame, consumed}` or `{consumed}`/needMore.
  - `decode(frame)` — Status (`af 13`), ControlConfiguration (`bf 24` → model/version), ControlConfiguration2 (`bf 2e` → accessory inventory), FilterCycles (`bf 23`), Ready (`bf 06`), NewClientCTS (`bf 00`).
  - encoders — `encodeToggleItem(item)`, `encodeSetTargetTemp(raw)`, `encodeControlConfigRequest(type)`, `encodeConfigRequest()`; item-code table.
- **`protocol.test.js`** — asserts decode/encode against authoritative frame fixtures (verified in Python+Ruby during the firmware build): Status→100°F/102°F/14:30/heating/pump1-hi/light1, ControlConfig→"BFBP20"/V17.0, ControlConfig2→2×2-speed pumps+light1, toggle-light1 = `7e 07 0a bf 11 11 00 93 7e`, set-temp-100 = `7e 06 0a bf 20 64 29 7e`, config-req = `7e 05 0a bf 04 77 7e`. **Runs with no hardware** — proves the decoder before plugging into the spa.
- **`spa-tester.js`** — interactive CLI (`serialport`): opens the port, streams bytes through `scanFrame`, keeps latest Status/Info/Config/Filter, and drives the guided flow.

## Guided flow
1. **① Monitor (read-only):** decode the ~1/sec Status broadcasts into a live line — `102°F → 104°F | heating | Ready | pump1:hi | light:on | 14:30` — plus frame + CRC-error counters. Pure listen; no transmit. Proves the bus + framing + decode.
2. **② Confirm model:** enqueue `ControlConfigurationRequest` (info + config2); send on the next `Ready`; display **model/version** and **detected accessories** (pumps & speeds, lights, blower, circ). Confirms spa identity and that transmit works.
3. **③ Perform action:** menu — toggle light, toggle pump 1, nudge target temp ±1°. Queue → send on next `Ready` → watch the next Status to **confirm the state changed** → report ✓/✗.

## Bus discipline & safety
- **Send only immediately after a `Ready` (`10 bf 06`)** — the shared half-duplex bus rule (same as the firmware); commands are queued and drained one-per-Ready as our client address `0x0A`.
- **Read-only until the user explicitly picks an action;** each action is confirmed before firing (it moves real spa equipment).
- To send (②/③) the tool acts as client `0x0A` — for reliable sends, no other client (ESP32/panel WiFi module) should also claim `0x0A`. Passive monitoring (①) needs none of that.

## Error handling
- Port open failure (missing device / not in `dialout`) → clear message.
- No data within a few seconds → prompt to check A/B wiring (and swap ±).
- CRC/garbage → counted and skipped (resync on next `0x7E`).
- Send with no state echo within a timeout → report, suggest checking we're the sole `0x0A` client / wiring.

## Testing
- `protocol.test.js` (offline) proves the codec against known frames — run first.
- Live integration = running `spa-tester.js` against the real spa (steps ①–③).

## Run
- Node 22 (`.nvmrc`), `npm install`, `node spa-tester.js` (defaults to `/dev/ttyUSB0`; `--port` to override).
