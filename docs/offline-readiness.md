# Readiness and review checklist

This PR combines source safeguards, MQTT changes, and operator-reported hardware
results. `PROJECT_STATE.md` is the current handoff. The status below separates
what code/tests establish from what still needs bus or physical evidence.

| Area | Current evidence | Still unproven |
| --- | --- | --- |
| Channel ownership | Correlated registration and queue invalidation have host coverage. | Exclusive ownership and full request/assignment/ACK wire trace. |
| Transport | Checked writes fail closed; a fault disables further writes for that boot. | Entire-driver hard timing bound and physical bus delivery. |
| Commands | TTL and observed-panel cancellation; no blind retry. Operator reports Light OFF and remote commands worked. | Command attribution, repeatable physical reliability, final Jets switch behavior. |
| MQTT / HA | Schema v2 documented; obsolete fan and extra Jets Power discovery records were cleared. | Cause of offline transitions after button commands; current retained availability correctness. |
| RS-485 events | Fixed bounded buffers, sampled events, loss counters and on-demand STATUS documented. | Lossless capture and external collector persistence. |
| Hardware compatibility | BP200G1/TP500 behavior is the target; earlier CNBP501X assumptions are called out. | Pump capability and Ready-in-Rest mapping on this exact pack. |

## Why the safeguards exist

The bus is shared and channel identity is weak: restored addresses and READY
matches cannot identify a client. Registration therefore requires correlation,
and ambiguity is left unresolved rather than guessed. Balboa control messages
are toggles without a transaction ACK, so uncertain writes are not retried.
High-rate MQTT publication previously filled the outbound queue; state updates are
bounded and diagnostic JSON is requested on demand. Details and tradeoffs are in
[`channel-ownership.md`](channel-ownership.md),
[`command-lifecycle.md`](command-lifecycle.md),
[`transport-liveness.md`](transport-liveness.md), and
[`mqtt-command-state-schema.md`](mqtt-command-state-schema.md).

## Before merge or deployment

Run the offline commands in `AGENTS.md`. Never run the MQTT/serial tester
entrypoints as tests. For hardware, use the separate gates and rollback steps in
[`bp200g1-commissioning.md`](bp200g1-commissioning.md); code coverage and local
diagnostic events do not replace direction-aware bus capture or physical checks.
