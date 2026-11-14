# Balboa BP200G1 / TP500 gateway state

Updated: 2026-11-14. Offline engineering only; physical active commissioning pending.

## Verified state and boundaries

- Board: Waveshare ESP32-S3 RS485-CAN. UART TX17, RX18, EN21, 115200 baud.
- Local device file: `balboa-passive.yaml`, `read_only: true`, `uart_selftest: false`.
- Operator-reported baseline: about 96 RX frames/s, zero UART TX, working TP500
  Jets/Light and HA state transitions. These are operator observations, not new
  measurements performed by this engineering run.
- Original firmware artifacts, local YAML, and the 30-second diagnostics package
  were hash-verified before builds. Rollback copies are in
  `.esphome/passive-baseline/`; no baseline outputs are used for new builds.
- Current base checkout was `fix/bp200g1-coexistence` at `187887c`, not the older
  `fix/rs485-channel-ownership` branch named in historical requests.
- Current work: `engineering/offline-commissioning`, separate commits/patches for
  ownership audit, provenance/transport outcome, command lifecycle, and readiness.
  Exact commit IDs and logs: `.esphome/offline-review/record.txt` and `patches/`.

## Current software guarantees

Restored addresses are observable only. TX authority requires a pending transmitted
request, matching full assignment, supported channel, and locally successful ACK
write. Conflicting assignment/transport loss revokes authority. Old queued frames
cannot survive ownership changes; queued forged handshake/flow-control frames are
rejected. Mandatory registered poll replies remain. Same-hash or delayed matching
responses cannot be distinguished reliably; physical ownership is not proven.

A final read-only UART guard covers self-test too. ESPHome uses checked UART-flush
and component-failure outcomes. Legacy void callback callers have weaker local
outcome knowledge. Non-idempotent toggles are not automatically retried.

Optional `rs485_events.yaml` provides bounded provenance with boot/sequence,
monotonic uptime and UTC only when system time is valid. It does not enable a
clock, active mode, or spa time sync. Original MQTT schema/topic stays unchanged.
`rs485_registration_only.yaml` suppresses app/config/time-sync writes while keeping
registration and mandatory idle replies; it never changes `read_only` to false.

Pending frames expire under a configurable local 5-second freshness policy and
unsent same-item toggles yield to observed other-client panel toggles. This does
not infer missed physical actions or provide closed-loop desired-state control.

## Open evidence, not assumptions

- Actual exclusive ownership and BP200G1/TP500 coexistence need approved bus evidence.
- Pump capability (single/two speed), observed raw speed cycle, Jets OFF physical
  behavior, and Ready-in-Rest transitions are not established by the cloned profile.
- Existing EC2 collector subscription and durable diagnostics persistence are UNKNOWN;
  no live query/deployment was performed. An offline adapter/integration plan exists.
- UTC is null without a valid configured system clock; collector receipt time is
  separate evidence. Sampled/overwritten events and network gaps must be considered.
- Reconciliation, retries, hash generation/relaxation and wire timing changes are
  deferred until evidence supports them; they are not solved by passing host tests.

## Next physical step

Review the readiness checklist and rollback image identity, then explicitly approve
only a passive candidate flash if desired. No active TX approval is implied.
Registration, coexistence, Light control, then Jets/heating tests are separate gates.
Do not deploy collector/HA/infrastructure changes as part of firmware verification.

Historical notes were preserved locally in
`.esphome/offline-review/PROJECT_STATE.previous.md`; obsolete hardware-unknown and
pre-passive commissioning claims have been superseded here.


## Observed-state publication follow-up

At `0ef5901`, status changes already notified entities immediately; the missing
item was a consolidated 10-second snapshot. The follow-up on the same branch adds
`balboa/waveshare/state`, fresh-observation/age metadata and tested field-wise
notification policy. No protocol/UART safety or timing code changed. Latest test,
commit and passive candidate records: `.esphome/status-review/`. Measured <=1s
HA receipt remains a passive commissioning acceptance test, not an offline result.
