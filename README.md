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
- `balboa-spa.yaml` / `packages/` — device config (tailored to a Canadian Spa Co.
  Cambridge: 1× 2-speed pump, 1× light; superset commented for other spas).
- `docs/` — wiring, bring-up, and the design spec.

## Bus addressing (important)
The RS-485 bus is shared and **addressed**. A client must ask the controller for a
channel and may then transmit *only* in a Ready addressed to that channel:

```
controller → FE BF 00           "any new clients?"
client     → FE BF 01 02 F1 73  ID request
controller → FE BF 02 <id>      assigns a channel (max 0x2f)
client     → <id> BF 03         ack
then: transmit only on <id> BF 06, as src <id>; send <id> BF 07 when idle
```

Never hardcode an address. Another client (the topside panel or a WiFi module)
already holds one — typically `0x10` — and answers *every* one of its Ready
windows, so a hardcoded client transmits on top of it and both frames are lost.

Because addressing is negotiated, **coexistence works**: this firmware runs happily
alongside the existing client, and the controller alternates polling between them.

The assigned channel is saved to NVS and resumed on boot. This matters: the
controller never reclaims a channel (there is no deregister message in the
protocol), so a gateway that re-negotiates on every reboot leaks one each time and
degrades the bus. If the spa stops polling our saved channel — e.g. after a spa
power cycle, which *is* what clears them — the firmware detects it and rejoins.

## Notes
- Cycle-2 filter is auto-enabled when its duration > 0 (no separate enable entity).
- `read_only: true` is fully passive: the firmware does not even join the bus.
- Heating mode is a sparse encoding — Ready-in-Rest is **3**, not 2.
- Requires a recent ESPHome (uses `climate_schema()`); see `docs/bring-up.md`.

## Credits
Protocol reverse-engineering from ccutrer's `balboa_worldwide_app` gem.
