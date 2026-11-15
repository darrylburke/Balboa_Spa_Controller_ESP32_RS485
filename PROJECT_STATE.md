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
- A v26 (`2026.11.15.26`) OTA trial enabled saved-channel resume. The operator
  reported TP500 Light responded but Jets did not. After OTA rollback to v11
  (`2026.11.15.25`, fresh registration), the operator reported both buttons
  responded. This comparison supports a resume-path regression but is not proof
  of cause; a 15-minute read-only MQTT capture received no device state,
  availability, or RS-485 event telemetry. OTA remained reachable.
- These are operator observations, not a wire capture. Internal registration,
  UART counters, command callbacks, and later state changes do not prove exclusive
  ownership, controller acceptance, or which client caused a physical change.

## Design decisions

- TX requires fresh correlated registration. This avoids trusting a saved slot
  after a possible spa reset, but can leak one slot on gateway-only reboot.
  Upstream commit `75c6055` reports hardware-verified resume and measured latency
  degradation after five leaked slots. The v26 field comparison supports keeping
  resume disabled on this BP200G1/TP500 installation. See `docs/channel-ownership.md`
  and `docs/bp200g1-commissioning.md`.
  Same-hash clients and delayed assignments remain ambiguous.
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
  investigate absent MQTT telemetry during the v26/v11 field trial and previous
  availability transitions; verify the final HA Jets switch separately; confirm
  persistence of any external collector. See
  `docs/offline-readiness.md` and `docs/bp200g1-commissioning.md`.
