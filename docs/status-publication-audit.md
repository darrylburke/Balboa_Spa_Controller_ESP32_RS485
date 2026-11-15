# Historical observed status publication audit

> Historical audit of the earlier snapshot implementation. Its conclusions about
> a consolidated 10-second `/state` JSON snapshot and 60-second entity refresh are
> superseded by MQTT schema v2: per-entity plain `/state` values publish on change
> and every 30 seconds, and typed JSON is on-demand on `/status`. See
> [the current schema](mqtt-command-state-schema.md).

Starting point: `engineering/offline-commissioning`, commit `0ef5901`.
No flash, active mode, bus traffic or live-service change was performed.

## Findings at 0ef5901

| Criterion | Code verdict | Evidence |
| --- | --- | --- |
| Immediate Jets/Light updates to existing entity topics | PASS for dispatch | `balboa_spa.cpp:54–76` called status callbacks on decoded changes; 60s was unchanged-state refresh, not debounce |
| Unchanged values may be deduplicated | PASS | Byte comparison against the previous notified status; switch layer also deduplicates |
| Consolidated fresh snapshot around every 10s | FAIL | No consolidated snapshot publisher; diagnostics were 30s and optional event batches 5s |
| Observed values, not optimistic command results | PASS | Fan/switch/climate/select read decoded parent status; filter optimistic publication had already been removed |
| Preserve passive guards and RS485 timings | PASS | Read-only/self-test flags and protected binaries were preserved |

The literal decode-to-HA receipt <=1s service target cannot be measured or certified
by this offline audit. Its software dispatch path has no configured debounce/wait;
ESPHome's MQTT fan/switch adapters defer to the main loop, then publish. Actual
broker/HA delivery, main-loop load and scheduling must be measured during the
separately authorized passive commissioning stage. A software PASS is not a measured
end-to-end PASS. No active commissioning is needed for that latency measurement.

## Final implementation and source evidence

- `components/balboa_spa/protocol/status_publication.h`: immediate change detection,
  field-wise equality (not struct-padding comparison), unchanged refresh at 60s,
  `SNAPSHOT_MS=10000`, freshness/observation bookkeeping.
- `components/balboa_spa/balboa_spa.cpp`: decoded status calls `observe()` and entity
  callbacks synchronously; independent `set_interval()` schedules JSON serialization
  outside UART processing. It publishes to the configured MQTT prefix + `/state`,
  i.e. `balboa/waveshare/state` here. Existing HA topics and diagnostic schema are
  unchanged. Multiple spa instances must configure distinct `state_snapshot_topic`
  overrides to avoid sharing the default snapshot topic.
- `components/balboa_spa/fan/balboa_fan.cpp:19`: observed raw speed maps to fan state
  and speed, then `publish_state()`; `switch/balboa_switch.cpp:23` publishes the
  observed Light boolean. Their control methods do not publish desired state.
- Installed ESPHome 2026.10.2 source: `components/mqtt/mqtt_fan.cpp:124` and
  `mqtt_switch.cpp:36` defer the state publish; `mqtt_fan.cpp:168` and
  `mqtt_switch.cpp:54` publish existing state topics. There is no multi-second
  change debounce in these adapters. They can coalesce pending updates; tests do
  not prove delivery of every transient when several frames arrive in one batch.

Snapshot schema v1 is non-retained. It includes boot/uptime/UTC when available,
`observation_seq`, observation time, age, bus connectivity and `fresh`. `status`
contains observed temperatures in Celsius, raw pump speeds, lights, aux, heating,
mode/scale/range, clock and accessory/status flags. It is null before observation,
when the bus is down, when the last observation is older than 10s, or when no new
Status arrived since the previous successfully queued snapshot. Unchanged freshly
received Status frames therefore still produce a fresh consolidated snapshot.

Publishing acceptance means local MQTT submission, not subscriber receipt.
Disconnected clients are skipped; no observation is consumed on publish failure.
There is no per-frame consolidated JSON or new UART operation. Protocol engine,
negotiation, queue policy, GPIOs, baud and driver delays are unchanged by this patch.
The bounded MQTT work adds CPU cost; physical timing remains unmeasured.

## Offline tests

`tests/test_status_publication.cpp` uses actual CRC-valid status frames through
ProtocolEngine in read-only mode and the same publication policy used by the
component. It checks Jets/Light notification in the decode call stack, 96 unchanged
frames deduplicated, zero UART writes, command receipt not changing observation,
10s snapshot policy, unchanged-but-fresh observations, missing/stale/disconnected
handling, publish-failure bookkeeping, unchanged refresh and 64-bit uptime.

Final results: 590 C++ checks / zero failures; 46 JS tests and 5 Python tests passed.
Both unchanged-config and provenance/registration-only variants compiled with
ESPHome 2026.10.2 in isolated build paths. Original baseline firmware/YAML and the
30s diagnostics package remain hash-identical. Exact final commit, logs, incremental
diff, compiled-passive flag checks and candidate hashes are in
`.esphome/status-review/`. The exact future passive upload command with rollback
checks is `.esphome/status-review/commission-passive.sh`; it was not executed.

## Passive acceptance measurement (future authorized stage)

After approving only the new passive candidate, verify zero UART TX and working
TP500 Jets/Light. Use an approved timestamped observer of the existing entity
state topics and `balboa/waveshare/state`. Confirm a fresh snapshot about every
10s, including during unchanged operation. Check `observed_uptime_ms` and
`status_age_ms`; do not treat retained old HA values or `status:null` as fresh data.

To establish <=1s from decoding to HA receipt, use a common-clock trace of decoded
status and subscriber/HA receipt (or a documented clock offset/error bound).
A stopwatch from the physical press includes controller status latency and is not
an exact decode-to-HA measurement. If clock provenance cannot establish that bound,
record the end-to-end target as not yet verified; do not infer it from host tests.
Only after passive acceptance may separately approved registration/coexistence and
control gates proceed. See `bp200g1-commissioning.md` for recovery and active limits.
