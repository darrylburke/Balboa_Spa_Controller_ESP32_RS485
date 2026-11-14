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
- `spa-serial-tester/` — Node bench CLI driving the same protocol from a laptop
  over a USB-RS485 adapter. This is where the protocol was actually proven against
  the real spa; useful for confirming the bus independently of the ESP32.
  See its own README.
- `spa-android/` — Kotlin/Compose phone app that monitors and controls the spa
  through this gateway's MQTT topics. See its own README.
- `balboa-spa.yaml` / `packages/` — device config (tailored to a Canadian Spa Co.
  Cambridge: 1× 2-speed pump, 1× light; superset commented for other spas).
- `docs/` — wiring, bring-up, and the design spec.

## Bus addressing (important)
The RS-485 bus is shared and **addressed**. A client must ask the controller for a
channel and may then transmit *only* in a Ready addressed to that channel:

```
controller → FE BF 00           "any new clients?"
client     → FE BF 01 02 F1 73  ID request
controller → FE BF 02 <id> F1 73  correlated assignment (0x10–0x2f)
client     → <id> BF 03         ack
then: transmit only on <id> BF 06, as src <id>; send <id> BF 07 when idle
```

Never hardcode an address. Another client (the topside panel or a WiFi module)
already holds one — typically `0x10` — and answers *every* one of its Ready
windows, so a hardcoded client transmits on top of it and both frames are lost.

A saved channel is retained for diagnostics only. It does not mark the engine
registered or permit idle replies, configuration requests, or commands. Active
registration requires an actually transmitted request, a full assignment response
with matching request hash bytes, and the existing ID_ACK. No new acknowledgement
message or timing delay is introduced. See [ownership limits](docs/channel-ownership.md)
before considering active commissioning; this correction is not hardware validation.

Commands submitted while passive or unregistered are discarded. Queued frames are
invalidated on ownership loss, restoration, and fresh assignment, so frames encoded
with an earlier channel cannot survive re-registration. Legitimately registered
clients still send one queued frame or the required idle reply on each matching poll.

## Temperature units
**Everything published is Celsius**, regardless of the spa's own scale setting —
the climate entity and both temperature sensors agree. Conversion happens only at
the protocol boundary (`spa_raw_to_celsius` / `celsius_to_spa_raw`).

This matters because an entity's `unit_of_measurement` is fixed at compile time
while the spa's scale can be changed at runtime (including via
`select/spa_temperature_scale`). Publishing the spa's native scale therefore
produced Celsius values labelled `°F` as soon as anyone flipped the setting — and
with `device_class: temperature`, Home Assistant would then "convert" them again.

`spa_raw_to_native()` still exists for display/diagnostics, but must never back an
entity. Front-ends that want °F should read `select/spa_temperature_scale/state`
and convert, or let Home Assistant do it via `device_class: temperature`.

## Notes
- Cycle-2 filter is auto-enabled when its duration > 0 (no separate enable entity).
- `read_only: true` suppresses protocol writes. Keep `uart_selftest: false` too;
  the final UART guard also blocks self-test writes in read-only mode.
- Heating mode is a sparse encoding — Ready-in-Rest is **3**, not 2.
- Requires a recent ESPHome (uses `climate_schema()`); see `docs/bring-up.md`.

## Credits
Protocol reverse-engineering from ccutrer's `balboa_worldwide_app` gem.

### RS485 diagnostics

To publish a non-retained MQTT snapshot every 30 seconds, include
`packages/rs485_diagnostics.yaml` under `packages` in a configuration with
`balboa_spa` ID `spa` and MQTT configured. Keep `read_only: true` and
`uart_selftest: false` for passive monitoring. The package does not change either
setting. The default topic is `balboa/waveshare/diagnostics/rs485`; edit it if needed.

Counters are cumulative since boot (uint32, wrapping), not rates. `assignments`
counts all observed assignment broadcasts; `last_assignment` is the raw assigned
byte, while `channel` is the engine's current channel (0 means unassigned).
`channel_adoptions`, `channel_restores`, and `channel_stale` distinguish correlated
registration, saved-address observation, and release of registered ownership.
Adoption is counted only after the ID_ACK write callback returns. Restoration
never establishes ownership; passive assignment broadcasts remain observations.

`ready` counts all polling windows, `ready_ours` counts those matching our current
internal channel (even in passive mode), and `ready_by_channel` gives nonzero counts keyed by hexadecimal channel
(00–2F). Windows outside that range still count in `ready` and appear in
`last_ready_channel`. Other channel fields are decimal numbers.

`tx_frames` and `tx_bytes` count completed UART write/flush calls, including the
optional UART self-test, rather than queued commands or attempted transmissions.
`last_tx_channel` and `last_tx_type` report bytes 2 and 3–4 of the last write
(type packed as a 16-bit number); they are null until a write has completed, and meaningful as protocol
metadata only with self-test disabled. These are software observations, not proof of RS485
wire delivery or controller acceptance. Passive operation with self-test disabled
should leave both TX counters at zero. No per-frame MQTT publishing or logging is
added to the bus processing path.


For passive commissioning, check `schema_version: 1`, `read_only: true`, and
`uart_selftest: false` in each snapshot. `rx_frames` counts decoded received
frames, including other clients' traffic; it is independent of `tx_frames`.
Observe several 30-second snapshots: increasing receive/poll counts with both TX
counters at zero is the expected software evidence of receive-only operation.
Assignments may increase without transmission; adoptions remain zero in passive
mode. `channel` may be nonzero because a saved address was restored.
`ready_ours` only means a poll matched that internal channel;
it does not identify the TP500 or establish exclusive ownership. Use uptime to
spot reboots before comparing counter deltas. Counters wrap at 2^32 and uptime
wraps with `millis()` after about 49.7 days. This build has not been flashed or
validated on the physical bus as part of these checks.

The JSON schema has 20 fixed top-level fields and at most 48 poll-map entries.
With compact serialization, maximum-width uint32 values and all channels present,
the payload stays below 2 KiB. Publication runs once per 30 seconds, with a fixed
48-iteration loop and no frame histories or growing strings. The receive path
only increments fixed-size counters; UART counters update after flush and driver
release. Diagnostics add no delays or protocol decisions; instrumentation and JSON
serialization still have a small CPU cost. Ownership rules are described above.


## Offline commissioning readiness

See [readiness and patch checklist](docs/offline-readiness.md),
[bounded event provenance](docs/rs485-provenance.md),
[command lifecycle findings](docs/command-lifecycle.md), and
[the staged commissioning/rollback procedure](docs/bp200g1-commissioning.md).
The optional versioned event package preserves the original diagnostic topic.
No offline test result establishes physical coexistence or authorizes transmission.


Observed Jets/Light changes notify existing MQTT entities immediately. A separate
non-retained `<mqtt topic_prefix>/state` snapshot publishes observed state around
every 10 seconds, with explicit freshness and observation age. It never waits for
that timer to notify changed entities. See [the publication audit](docs/status-publication-audit.md)
for source/test evidence and the unmeasured one-second HA delivery target.
