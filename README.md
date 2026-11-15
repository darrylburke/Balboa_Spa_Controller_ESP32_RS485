# Balboa ESP32 MQTT gateway

ESPHome firmware that reads and controls Balboa spa packs over RS-485, publishes
Home Assistant entities through MQTT, and implements the Balboa protocol in C++.

## Start here

1. Read [the engineering constraints](AGENTS.md) and
   [current project state](PROJECT_STATE.md).
2. Copy `secrets.yaml.example` to `secrets.yaml` and fill in local credentials.
3. Run the offline checks listed in `AGENTS.md`.
4. Review [bring-up](docs/bring-up.md) and the
   [BP200G1 commissioning and rollback guide](docs/bp200g1-commissioning.md)
   before any hardware action. A successful compile or test does not authorize a
   flash or prove safe bus coexistence.

The tracked `balboa-spa.yaml` is a legacy ESP32-C3 configuration, not the local
Waveshare ESP32-S3 profile used with the BP200G1/TP500. Device-specific YAML,
secrets, rollback images, and capture files are local and must not be committed.

## Important behavior

The RS-485 bus is shared. A saved address or a matching READY frame is not proof
of ownership. Transmission requires a fresh correlated registration; see
[channel ownership](docs/channel-ownership.md). Commands use Balboa toggles with
no transaction ACK, so the gateway expires queued actions and does not blindly
retry or reconcile them; see [command lifecycle](docs/command-lifecycle.md).

MQTT schema v2 sends plain per-entity state for Home Assistant and on-demand typed
JSON on `/status`. See the [MQTT schema](docs/mqtt-command-state-schema.md).
Passive diagnostics keep their existing schema; bounded event provenance is
documented in [RS-485 provenance](docs/rs485-provenance.md).

The default real-spa package exposes pump 1 as the `Spa Jets` on/off switch: ON
requests low speed and OFF requests stopped. This gives HA a state-backed on/off
control instead of presenting pump speed as a fan control. Choose the fan platform
when speed selection is wanted. This is an entity choice, not a claim that every
pack has the same pump capability.

## Repository layout

- `components/balboa_spa/` — ESPHome component and protocol implementation.
- `packages/` — reusable entity and diagnostics packages.
- `tests/` — offline C++, JavaScript, and Python tests.
- `spa-serial-tester/`, `spa-mqtt-tester/`, `spa-simulator/` — protocol tools;
  do not use live tester entrypoints as tests.
- `docs/` — design rationale, commissioning gates, MQTT schema, and recovery.

Protocol reference: [ccutrer/balboa_worldwide_app](https://github.com/ccutrer/balboa_worldwide_app).
