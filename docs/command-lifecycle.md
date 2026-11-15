# Remote-command findings and bounded safeguards

The BP200G1's physical speed/mode behavior is not established by the repository's
historical CNBP501X profile. This review distinguishes source arithmetic from
physical evidence; operator-reported results and remaining gaps are in
[`PROJECT_STATE.md`](../PROJECT_STATE.md).

## Confirmed source behavior and defects

The TP500 can cause an item-toggle frame on the shared bus, but the frame does not
authenticate which client caused it. If a Waveshare toggle is queued from the last
observed state and another client changes that item before the next READY, the
queued toggle can undo that action. This is why matching unsent toggles are
cancelled, commands expire, and uncertain commands are never retried.

- MQTT Jets OFF reaches `BalboaFan::control()` with state=false, then
  `ProtocolEngine::set_pump(index, 0)`. It is not a direct hardware OFF command.
  Balboa item requests are toggles. The implemented count is
  `(desired-current+(levels+1)) % (levels+1)`, using the received inventory capability.
- Under that model, two-speed low->off requires two toggles; high->off requires one;
  a one-speed on->off requires one. Host tests cover every current/desired pair
  for capability 1 and 2. This tests the arithmetic, not the BP200G1's actual cycle.
- Before this patch, the MQTT tester sent ON and then speed for one Jets action.
  Both can enqueue toggles against the same old status, causing duplicate actions.
  Its pure command planner now emits exactly one desired command per Jets action.
- Known command payload shapes are checked before queue admission; malformed
  CRC-valid requests cannot masquerade as implemented toggle/set commands.
- Queue entries previously had no age limit and could execute on delayed polls.
  A local `command_ttl` policy now defaults to 5s. It applies to queued frames,
  including config requests; expiration drops them without retry. This is a
  configurable freshness bound, not a claimed Balboa timing requirement. A multi-
  toggle action may partially execute before expiry; no rollback/replan is inferred.
- Observed other-client same-item toggle frames now cancel unsent queued toggles
  for that item. No automatic desired-state reassertion follows. This yields to
  the physical panel where its action was actually received; missed frames and
  simultaneous already-sent actions cannot be undone or attributed reliably.
- Filter-cycle commands formerly overwrote the engine's observed state and the
  number entity published the requested value immediately. Both optimistic writes
  are removed. Only a received controller response publishes observed filter state.
  Consecutive field edits must wait for observations; there is no merge/retry loop.

## Interpretation and limits

The configured `speed_count: 2` is a static entity trait inherited from the old
profile. It is not discovery of BP200G1 capabilities. The decoder takes pump1
capability from config payload d[0]&3 and status from status payload d[11]&3.
Speed raw=2 does not by itself prove a two-speed motor. An OFF fan may publish a
remembered speed level of 1 while state=OFF; use the state field too. Do not change
the trait, clamp raw status differently, or assume speed 3 without actual inventory
and timestamped panel cycles. Missing config blocks engine pump commands.

Heating-mode status values are sparse: READY=0, REST=1, READY_IN_REST=3. The select
currently reports READY_IN_REST as `ready`. Existing logic uses one toggle between
READY/REST and two from READY_IN_REST to READY. Whether that transition is correct
for this BP200G1 is not proven. No new heating-mode mapping or retry was invented.

Repeated desired commands before a status update can still compute duplicate
non-idempotent toggles. The firmware does not reconcile desired state, coalesce
all producers, identify MQTT transactions, guarantee cancellation after TX, or
prove command execution. TTL and observed-panel cancellation reduce specific
risks; they are not a reliable closed-loop controller. External producers, retained
commands and QoS duplicates remain risks. Do not enable unattended control.

The tester invalidates pending verification and cached state on disconnect, rejects
retained/unrelated/late responses as command evidence, and reports a matching
observation rather than physical confirmation. It does not replay a pending action.
These are offline-tested helper rules; the interactive tool was never connected.

## Deferred reconciliation design (requires evidence)

A future per-item desired-state transaction would need one outstanding operation,
a local deadline, one toggle at a time, a newer relevant status before planning
another, explicit cancellation on ownership/bus loss or panel action, and no blind
retry of an uncertain write. A matching state is an observation, not a toggle ACK.
If a status can be delayed or a panel action missed, inference cannot safely tell
whether another toggle advances the user's goal or reverses a physical action.

Before implementing it, collect raw inventory, status and panel-toggle traces for
Light, single/two-speed Jets, OFF, READY/REST/READY_IN_REST and automatic heating
or filtration interventions. Record status latency and duplicate/reconnection cases.
The reported Light OFF proves one physical effect only; it does not resolve command
attribution or establish Jets/heating behavior. Keep unattended reconciliation
deferred until those facts are available.
