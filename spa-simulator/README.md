# spa-simulator

A **virtual Balboa spa** that publishes the same MQTT surface the ESPHome gateway
publishes. Built so dashboard and panel work can continue while the RS-485
hardware is unavailable — anything built against the simulator works unchanged
against the real device.

It is **not** an RS-485 emulator. It replaces the gateway, it does not test it.

## Run

```sh
npm install
node spa-sim.js              # appears in HA within seconds
node spa-sim.js --fast 120   # 120x clock: a 2h filter cycle runs in 1 minute
node spa-sim.js --clean      # remove it from HA entirely, then exit
```

Credentials are read from `../secrets.yaml`, so they always match the real device.
Override with `--broker`, `--port`, `--secrets`, `--prefix`, `--discovery-prefix`.

It must keep running to stay "online" in HA — it publishes an MQTT LWT, so if you
stop it the device correctly goes unavailable rather than showing stale values.

## Isolation from the real device

| | Real | Simulator |
|---|---|---|
| Device | `balboa-spa` | `balboa-spa-sim` ("Balboa Spa (Simulated)") |
| State prefix | `spa/` | `spa-sim/` |
| Discovery | `ha/<comp>/balboa-spa/…` | `ha/<comp>/balboa-spa-sim/…` |
| unique_id | ESPHome-generated | `spasim_<object_id>` |

Both can run at once; they appear as two separate devices in Home Assistant.

## Behaviour

- **Heater** engages when `current < target − 0.5 °C` and the mode allows; warms at
  ~0.5 °C/min, cools at ~0.1 °C/min otherwise, and never overshoots the setpoint.
- **Rest mode** suppresses heating entirely, so the mode select visibly matters.
- **Filter cycle 1** runs on its configured start hour and duration (handles the
  midnight wrap), driving the filter-running and circulation-pump sensors.
- **Commands** are applied and echoed back as state, including `TOGGLE` on
  switches — the same feedback loop the real device gives.

Reports **CNBP501X / V36.0** with pump1 (2-speed) + light1, matching the real spa.

## Testing the dashboard's failure path

The bus-connected sensor has a hidden command topic, so you can simulate the exact
failure that cost a day of bring-up — the gateway online but the spa unreachable:

```sh
mosquitto_pub -h <broker> -u <user> -P <pass> \
  -t 'spa-sim/binary_sensor/spa_bus_connected/command' -m OFF
```

A correct dashboard should grey out or warn. Send `ON` to restore.

## Units

**All temperatures on the wire are Celsius**, exactly like the real device.
`select/spa_temperature_scale` is a display hint only — use it to decide what to
render, not to interpret the values.

## Layout

- `model.js` — pure state machine, no MQTT. All behaviour lives here.
- `entities.js` — the entity table: discovery payloads, state topics, command routes.
- `spa-sim.js` — thin MQTT adapter and tick loop.
- `model.test.js` — 16 tests, no broker required (`npm test`).
