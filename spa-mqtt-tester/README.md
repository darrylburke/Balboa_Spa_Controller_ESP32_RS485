# spa-mqtt-tester

Bench tester for the spa over **MQTT** — the counterpart to `../spa-serial-tester`,
which drives the RS-485 bus directly. Same UX, different transport: this one
exercises the surface the ESP32 gateway actually publishes, so it is what you use
to verify the gateway end-to-end after flashing.

## Setup
```sh
npm install
node mqtt-tester.js
```

Credentials are read from `../secrets.yaml` (`mqtt_broker`, `mqtt_username`,
`mqtt_password`) so they live in one place and always match the firmware. Override
with `--broker`, `--user`, `--pass`, `--port`, `--secrets <file>`, `--prefix <p>`.

## Keys

| key | action | moves equipment? |
|---|---|---|
| `m` | counters + availability + bus state | no |
| `d` | dump every known topic and value | no |
| `c` | model / firmware / scale / filter / flags summary | no |
| `1` | toggle Light | **yes** |
| `2` | cycle Jets (off → low → high → off) | **yes** |
| `+` / `-` | target temperature ±1 step | **yes** |
| `r` | heating mode READY ↔ REST | **yes** |
| `f` | set filter 1 start hour / duration (**persistent**) | **yes** |
| `n` / `o` | press Clear Notification / Normal Operation | **yes** |
| `h` / `q` | help / quit | no |

`--yes` (or `-y`) skips the y/N confirmation on everything that acts, including
the filter write. Commands still log the exact topic and payload, and are still
verified against the state echo.

## What it checks that a raw `mosquitto_sub` does not

- **Refuses to send when the spa is unreachable.** Publishing a command to an
  offline gateway silently does nothing useful, so the tool blocks it when
  `status != online` or `spa_bus_connected == OFF` and says which.
- **Distinguishes stale from live.** Every state topic is retained, so a
  disconnected gateway leaves a complete-looking set of values on the broker. The
  status line says `⚠ spa OFFLINE — values below are retained/stale` rather than
  showing them as current.
- **Verifies commands against the echo**, printing `✓ confirmed` or `✗ no
  confirming state`, instead of assuming a publish took effect.
- **Publishes commands with `retain: false`.** A retained command would be
  redelivered every time the gateway reconnects and would move real equipment
  unprompted. Never publish commands retained.

## Temperature units

Everything on the wire is **Celsius**. The tool displays in the spa's own scale
(read from `select/spa_temperature_scale/state`) and converts on the way in and
out, so `+`/`-` steps 1 °F when the spa is in Fahrenheit and 0.5 °C otherwise.

## Topics

State is `{prefix}/{component}/{object_id}/state`, commands are the same path with
`/command`, and availability is `{prefix}/status` (`online`/`offline`, retained
LWT).

**Gotcha:** ESPHome publishes *text* sensors under the `sensor` component type,
so model/firmware/notification are at `sensor/spa_model/state` — **not**
`text_sensor/...`. Verified against a live device; assuming otherwise silently
yields no data.

All temperatures on the wire are Celsius (`unit_of_meas: °C` in discovery), and
every entity carries `avty_t: {prefix}/status`.
