# Virtual spa simulator (MQTT) — design

**Date:** 2026-07-27
**Status:** approved

## Problem

The RS-485 hardware is blocked waiting on replacement transceiver boards (~2 weeks).
Meanwhile the Home Assistant dashboard and the SaaS panel both need a spa to talk to.
Designing a dashboard against `unavailable` entities is guesswork.

## Solution

A virtual spa that publishes the **exact MQTT surface the ESPHome gateway publishes**,
so anything built against it works unchanged when real hardware arrives.

Deliberately **not** an RS-485 emulator. That is a separate, larger job (it must
emulate the controller's Ready round-robin and the channel handshake) and it does
not unblock dashboard work. Note the reference gem's `bwa_server` does not help
here: it is a TCP server on port 4257 emulating the Balboa *WiFi module*, not the
RS-485 bus.

## Identity and isolation

Runs against the **real broker** so it is viewable in HA, but cannot collide with
the real device:

| | Real device | Simulator |
|---|---|---|
| Device name | `balboa-spa` | `balboa-spa-sim` |
| State prefix | `spa/` | `spa-sim/` |
| Discovery | `ha/<comp>/balboa-spa/…` | `ha/<comp>/balboa-spa-sim/…` |
| unique_id | ESPHome-generated | `spasim_<object_id>` |

Discovery prefix is `ha` — this HA instance's MQTT integration uses `ha`, not
ESPHome's default `homeassistant`.

## Entity set

All 20 entities mirrored exactly: same component types, same object-id pattern,
**Celsius on the wire**, same payloads (`ON`/`OFF`, `heat`/`off`, `ready`/`rest`,
speed `1`/`2`, `PRESS`). Text sensors publish under the **`sensor`** component,
not `text_sensor` — verified against the live device.

Reports **CNBP501X / V36.0**, pump1 (2-speed) + light1, matching the real spa.

## Behaviour model

- **Temperature** drifts toward setpoint at ~0.5 °C/min while heating; loses
  ~0.1 °C/min otherwise.
- **Heater** engages when `current < target − 0.5` and the mode permits;
  `action` reports `heating` / `idle`.
- **Rest mode** suppresses heating, so the mode select visibly changes behaviour.
- **Filter cycle 1** runs at its configured start hour for its duration, driving
  `filter_cycle_1_running` and the circulation pump.
- **Jets / light / hold** are direct manual state.
- **Commands** are subscribed, applied, and echoed back as state — the same
  feedback loop the real device provides.

## Decisions taken

- **State topics are retained**, matching the real device: HA repopulates
  instantly on restart. Cost: stopping the sim leaves values on the broker until
  `--clean`, though `status` correctly flips to `offline` via LWT.
- **`spa_bus_connected` is forceable to OFF** via a hidden command topic, so the
  dashboard's stale/disconnected path can be exercised — the exact failure mode
  that consumed a day of bring-up.
- Commands are published/handled **non-retained**; a retained command would
  re-fire on every reconnect.

## Structure

```
spa-simulator/
  model.js        pure state machine — no MQTT
  entities.js     entity table + discovery payload builders
  spa-sim.js      MQTT adapter + tick loop + --clean
  model.test.js   unit tests for the state machine
```

The model is pure so it is testable without a broker: temp drift, heater
hysteresis, rest-mode suppression, filter scheduling, command application.

## CLI

```
node spa-sim.js [--prefix spa-sim] [--fast 60] [--clean]
```

`--fast N` scales the clock (a 2-hour filter cycle runs in 2 minutes).
`--clean` removes every retained state and discovery topic it created, leaving
no trace in HA.

## Out of scope

RS-485 emulation; multi-spa simulation; fault injection beyond the bus-connected
toggle.
