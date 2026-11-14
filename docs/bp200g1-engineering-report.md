# BP200G1 / TP500 engineering report

The initial root-cause review and ownership correction were committed as `187887c`.
The subsequent offline audit is recorded in [the readiness checklist](offline-readiness.md),
with final commit IDs, exact test/build results, hashes and separated patch series
in `.esphome/offline-review/`. The preserved earlier record remains under
`.esphome/coexistence-review/`.

Confirmed defects included restored identity granting authority, unsolicited
assignment adoption, stale queued addresses, public-queue handshake injection,
transport loss retaining authority, and treating failed ACK writes as successful.
These are source/test findings. The TP500's physical ownership of 0x10 and the
precise cause of earlier electrical collisions remain hypotheses.

Fixes preserve registration messages, supported correlation checks and mandatory
registered polls. Final UART read-only protection covers every local entry point.
Checked UART outcomes revoke authority on local failure, without retrying toggles.
Optional bounded event provenance exposes local timestamps/order/outcomes; legacy
30-second MQTT fields remain unchanged. No source change supplies proof of wire
receipt, exclusive ownership, controller acceptance, or physical command execution.

[Command findings](command-lifecycle.md) distinguish proven queue/tester issues from
unverified pump and heating-mode behavior. TTL and observed-panel cancellation are
bounded safeguards, not speculative closed-loop reconciliation. The registration-
only gate suppresses application/config/time-sync writes while preserving handshake
and idle replies. It does not enable active mode.

Residual risks include same-hash and delayed assignments, missed conflicting
reallocation, buffered/late poll handling, electrical/driver timing, and uncertain
or duplicate non-idempotent commands. Those require the [staged procedure](bp200g1-commissioning.md):
registration evidence, coexistence with responsive TP500, single Light action, then
only evidence-supported Jets/heating tests. No physical stage was executed.

The EC2 collector's receipt/persistence is UNKNOWN; an [undeployed integration](rs485-collector.md)
is supplied without changing credentials, services, infrastructure or Home Assistant.
The known-good passive configuration and binary hashes are preserved. The spa has
not been declared fixed or ready for unattended control.
