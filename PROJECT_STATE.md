# Current project state

Updated 2026-11-15. Read this first when continuing work.

## Hardware and reported operation

- Target: Waveshare ESP32-S3, Balboa BP200G1 pack, TP500 panel; UART TX17/RX18,
  direction GPIO21, 115200 baud.
- The local `balboa-passive.yaml` remains read-only with UART self-test disabled.
  Never include it, its backups, secrets, or `.esphome/` artifacts in a PR.
- Operator reported a successful OTA of control candidate v11 (`2026.11.15.25`),
  successful Light OFF, and working remote commands. The final Jets HA entity is
  now a switch; its post-migration physical behavior has not been confirmed here.
- Operator observed brief MQTT `offline` transitions after Normal Operation and
  Clear Notification button requests, followed by recovery. Cause is unknown.
  No live device check was made while preparing this PR.
- These are operator observations, not a wire capture. Internal registration,
  UART counters, command callbacks, and later state changes do not prove exclusive
  ownership, controller acceptance, or which client caused a physical change.

## Design decisions

- A saved channel or READY match is not ownership. TX requires a fresh correlated
  registration; ambiguity from same-hash clients and delayed assignments remains.
- Toggle commands are not blindly retried or reconciled: there is no command ACK,
  and a retry can reverse an action that already happened. Queues expire and yield
  to observed panel actions, but are not a closed-loop controller.
- HA receives plain per-entity state. Typed diagnostic JSON is on-demand on
  `/status`; no aggregate `/state` JSON is part of schema v2. See
  `docs/mqtt-command-state-schema.md`.
- Event rings, MQTT retry pacing, and transport fault handling are bounded. A local
  write result is not proof of wire delivery. See `docs/rs485-provenance.md` and
  `docs/transport-liveness.md` for the tradeoffs and limits.

## Current review and next work

- Review branch: `pr/bp200g1-reliability-mqtt` in the fork. Check GitHub for the
  current PR and branch state before proceeding.
- Before merge, run the offline C++, Node, and Python commands in `AGENTS.md`.
  Live serial/MQTT testers are not tests and can transmit.
- Remaining evidence: complete direction-aware registration/coexistence capture;
  verify the final Jets switch physically; investigate MQTT availability transitions;
  confirm persistence of any external collector. See
  `docs/offline-readiness.md` and `docs/bp200g1-commissioning.md`.
