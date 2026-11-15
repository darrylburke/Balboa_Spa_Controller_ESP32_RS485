# BP200G1 / TP500 engineering summary

This PR fixes unsafe assumptions in the shared-bus client and adds typed MQTT
status, bounded event provenance, and checked UART writes. The first ownership
correction is in commit `187887c`; the reviewable source history is the PR commit
series. Test/build evidence and limits are in
[`offline-readiness.md`](offline-readiness.md).

The main design choices are deliberate:

- A stored channel is not identity. Only a correlated fresh registration grants
  TX authority; same-hash and delayed responses remain ambiguous.
- Balboa commands are toggles without an ACK. The queue has a freshness limit and
  no blind retry or automatic reconciliation, because a repeated toggle can undo
  a successful but unobserved action.
- MQTT entity state stays plain and per-entity for HA. Human-readable runtime JSON
  is on-demand on `/status`; high-rate diagnostics previously contributed to
  outbound queue pressure.
- UART failures revoke authority and latch writes off for that boot. This avoids
  sending a buffered tail after a timeout; the bound is local to the IDF drain,
  not a controller timing guarantee.

The operator reports v11 OTA success and a successful Light OFF. That is useful
physical evidence, but no direction-aware bus trace is attached to this PR. It
does not establish exclusive ownership, controller acceptance, or reliable
unattended control. Other gaps and next evidence are listed in
[`PROJECT_STATE.md`](../PROJECT_STATE.md) and the
[`commissioning guide`](bp200g1-commissioning.md).
