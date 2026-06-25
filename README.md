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
