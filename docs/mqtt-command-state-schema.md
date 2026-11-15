# MQTT command/state schema

Schema v2 separates Home Assistant entity state, gateway availability, and
diagnostic JSON:

| Topic | Direction | Purpose |
| --- | --- | --- |
| `balboa/waveshare/command` | subscriber | Light commands and on-demand STATUS/DEBUG requests |
| `balboa/waveshare/<component>/<entity>/state` | publisher | Plain per-entity state values for HA |
| `balboa/waveshare/status` | publisher | Typed JSON COMMAND, CONFIG, STATUS, and DEBUG messages |
| `balboa/waveshare/availability` | publisher | ESPHome birth/LWT availability for HA; retention is set in the device YAML |

ESPHome discovery configuration is retained. Entity state values are not retained;
HA stores received values in its own state/history database. The root `/state`
topic does not carry a consolidated JSON envelope.

## Message types

Every JSON message on `balboa/waveshare/status` has `schema_version: 2` and a
`message` discriminator:

| `message` | When it is published | Main contents |
| --- | --- | --- |
| `COMMAND` | After a supported root text command is parsed locally (currently `LIGHT`) | Command, value, index, original request, and observed spa state |
| `CONFIG` | When spa model/equipment configuration is decoded | Model/version and reported equipment capabilities |
| `STATUS` | Only in response to `STATUS <value>` | Requested runtime/diagnostic sections; echoes the selector |
| `DEBUG` | In response to `DEBUG <value>` | Bounded boot/runtime details or a bounded RS-485 event batch |

STATUS JSON is on demand. Per-entity state topics publish immediately when their
observed value changes and refresh every 30 seconds while valid spa status is
arriving. A STATUS request also immediately refreshes the per-entity topics for
HA. Entity values are plain payloads, such as `-58` on
`balboa/waveshare/sensor/waveshare_wifi_rssi/state`; they do not carry a JSON
message discriminator.

The plain `online`/`offline` payload on `/availability` is separate from the JSON
`STATUS` message on `/status`. Discovery configs point HA at `/availability`.

This split keeps HA state simple and gives people/tools a readable diagnostic
response without sending an aggregate JSON state on every update.

## Commands

`LIGHT [index] ON|OFF` requests a Light state. The index is one-based and optional;
omitted means Light 1. For example, `LIGHT OFF` and `LIGHT 1 OFF` are equivalent.
The gateway accepts this only when active, freshly registered, and holding recent
spa status. It queues the Balboa toggle in the next READY window for its assigned
channel. If the observed Light already has the requested value, the idempotency
guard queues no RS-485 frame.

`STATUS <value>` requests a diagnostic section. Valid values are `0` (all), `1`
(firmware/runtime), `2` (Wi-Fi), `3` (MQTT), `5` (fresh spa state), and `8`
(RS-485 counters/registration).

`DEBUG <value>` requests bounded debug data. `0` returns boot/runtime fault trace
and heap information; `8` returns the pending bounded RS-485 event batch and loss
counters. Debug is on demand; it is not a per-frame MQTT log stream.

Example read-only request:

```sh
mosquitto_pub -h "$MQTT_BROKER" -t balboa/waveshare/command -m 'STATUS 8'
```

STATUS and DEBUG are read-only and also work in passive mode. Unsupported commands
or selectors are ignored. Commands must not be retained. The root text command
currently controls Light. HA entities, including buttons, Jets, and heating
controls, use their own component command topics; they do not produce a `COMMAND`
JSON response. A button request followed by `/availability` `offline` therefore
does not establish whether the action ran or why MQTT disconnected. The operator
observed recovery after Normal Operation and Clear Notification requests; the
cause is unknown.

The `Spa Jets` HA switch controls pump 1 as an on/off toggle: ON requests low speed,
and OFF requests stopped. No separate HA fan entity is exposed for pump 1.

## STATUS format

STATUS responses are non-retained JSON on `/status`. `fresh` means a same-boot spa
observation exists and is no older than the bus timeout; it does not mean a
command was accepted by the controller.

```json
{
  "schema_version": 2,
  "message": "STATUS",
  "command": "STATUS",
  "value": 8,
  "boot_id": 1282029153,
  "uptime_ms": 123456,
  "status": {
    "rs485": {
      "read_only": false,
      "uart_selftest": false,
      "registered": true,
      "registration_only": false,
      "channel": 18,
      "rx_frames": 100,
      "new_client_cts": 3,
      "assignments": 1,
      "ready": 500,
      "ready_ours": 120,
      "tx_frames": 12,
      "tx_bytes": 96
    }
  }
}
```

Selector `8` returns the RS-485 counters previously published periodically on
`balboa/waveshare/diagnostics/rs485`. The active candidate retires that standalone
periodic topic; request `STATUS 8` to retrieve the counters. The preserved passive
configuration remains available as the rollback image and retains its existing
diagnostic package.

## Other JSON formats

### COMMAND

```json
{
  "schema_version": 2,
  "message": "COMMAND",
  "command": "LIGHT",
  "value": "OFF",
  "index": 1,
  "phase": "PARSED",
  "request": "LIGHT 1 OFF",
  "boot_id": 1282029153,
  "status": { "lights": [true, false] }
}
```

This reports local parsing and the spa state observed at receipt. It is not a
controller ACK and does not prove UART delivery or physical execution.

### CONFIG

`CONFIG` contains `info_valid`, `config_valid`, `model`, `version`, and an
`equipment` object with pump speed counts, light/aux presence, blower level, mister,
and circulation-pump capabilities. ESPHome also republishes the HA discovery
configuration.

### DEBUG

DEBUG responses echo `command: "DEBUG"` and the numeric selector in `value`, and
place bounded diagnostic details under `debug`. DEBUG 8 contains at most 16 local
events. Local event records do not prove wire delivery, controller acceptance, or
physical execution.

## Delivery and retention

Entity state, typed `/status` JSON, and commands use QoS 0 and are non-retained.
Discovery configuration and `/availability` may be retained. The firmware
sets birth and last-will retention in its ESPHome YAML. In the recent production
trial, the offline last-will was retained but the online birth was not; the broker
could therefore keep a stale `offline` value after the device reconnects. Retain
the online birth and verify the broker's retained availability before relying on
HA availability. The firmware
publishes per-entity values through the bounded ESPHome MQTT queue and ESP-MQTT's
native outbox. Queue admission is local only; it is not a broker or spa ACK. The
standard ESPHome subscription callback does not expose the MQTT RETAIN bit, so
command publishers must enforce the non-retained command rule. Never retry a
toggle from a timeout; read the next fresh spa state instead.

Schema v2 is opt-in through `balboa_spa: mqtt_command_state: true`. The preserved
`balboa-passive.yaml` and its rollback behavior are unchanged.

### Upgrade cleanup

When migrating from the earlier firmware, delete obsolete retained values from
`balboa/waveshare/status` (previous availability), `balboa/waveshare/debug`
(previous log output), and `balboa/waveshare/diagnostics/boot`. Clear any retained
per-entity state values as well; schema v2 entity states are non-retained. This
deployment also tombstoned `homeassistant/fan/waveshare-balboa/spa_jets/config`
and `homeassistant/switch/waveshare-balboa/spa_jets_power/config`. The retained
`homeassistant/switch/waveshare-balboa/spa_jets/config` now describes the single
on/off Jets switch. The old state values and three legacy root topics above were
also tombstoned.
