# Balboa gateway engineering constraints

- Never flash, reboot, transmit to the spa, enable active mode, change wiring, or
  alter live MQTT/EC2/HA services without explicit operator authorization.
- Preserve `balboa-passive.yaml`: `read_only: true`, `uart_selftest: false`, TX17,
  RX18, EN21, 115200. It and secrets are local files, not upstream patch content.
- Before any build verify `.esphome/passive-baseline/SHA256SUMS` and the baseline
  hashes in `.esphome/coexistence-review/baseline-hashes.json`. Build only outside
  `.esphome/build/waveshare-balboa` using `ESPHOME_BUILD_PATH`.
- A restored channel or READY is not ownership. Preserve registration correlation,
  fail-closed handling, mandatory registered replies, and queue epoch invalidation.
- Do not invent registration identity guarantees, ACKs, or response timings.
  Same-hash/delayed assignment ambiguity needs measured evidence.
- No unbounded per-frame MQTT/logging. Existing `diagnostics/rs485` schema stays
  unchanged; optional provenance uses `diagnostics/rs485/events/v1`.
- Test offline: `make test`; `node --test spa-serial-tester/protocol.test.js
  spa-simulator/model.test.js spa-mqtt-tester/command-plan.test.js`;
  `python3 -m unittest discover -s tests -p 'test_*.py'`.
- Do not run the MQTT/serial tester entrypoints for tests: they can connect/send.
  Pure modules and their test files are safe. Never log credentials or secrets.
- Current controller is BP200G1/TP500, not the repository's historical CNBP501X
  profile. Pump speed capability and Ready-in-Rest behavior require evidence.
- Use the requirement checklist and commissioning gates in `docs/offline-readiness.md`
  and `docs/bp200g1-commissioning.md`. Read-only local events do not prove wire
  delivery, controller acceptance, exclusive ownership, or physical execution.
