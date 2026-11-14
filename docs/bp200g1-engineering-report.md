# BP200G1 / TP500 engineering review

## Outcome and scope

The offline patch separates saved identity from registration authority, correlates
assignment responses, and prevents frames from an old ownership epoch escaping.
Physical coexistence is not established by host tests. No flash, active-mode enable,
spa command, hardware change, or live-environment change was performed.

Root causes established in the previous implementation:

- Restoration made any persisted nonzero address registered. Matching READYs kept
  it alive without proving ownership. Active idle replies alone could interfere.
- Any unregistered client adopted broadcast assignments, including while passive,
  and persisted them without correlating a transmitted request.
- Complete queued frames retained their original address and survived ownership
  loss; the public queue also accepted foreign-address and malformed frames.

These are demonstrated software defects. That the TP500 owns `0x10` and that its
responses collided with previous gateway transmissions remain strong hypotheses,
not a recorded on-wire diagnosis. Passive TX=0 with a working TP500 supports the
hypothesis but does not identify a channel owner.

## Implemented corrections and review

`registered_` is distinct from observable `channel_`. Restoring an address never
authorizes TX. Assignment adoption requires a pending transmitted request, a full
matching response in the supported polling range, and the existing ACK callback.
Unsolicited, mismatched, short, and invalid assignments do not establish authority.
Distinguishable competing join requests cancel pending registration; a conflicting
assignment of the owned channel revokes authority immediately; there is no
new delay or backoff. Genuine registered polls still get queued or idle replies.

Ownership loss/restoration/registration and entering passive mode clear the queue.
Passive/unregistered commands cannot queue for later replay. Queue admission
requires one complete CRC-valid frame for the current channel, without parser
resynchronization noise. Malformed flow-control payloads cannot authorize TX.
Tests rebuild when protocol or test headers change.

Review checked all UART write entry points, saved-channel callbacks, queue admission
and retirement, parser boundaries, and the assignment/READY guards. The existing
self-test UART bypass is still disabled. Diagnostic JSON keys and publication
interval are unchanged. Hash verification protects the preserved config/artifacts.
The separate JS serial bench tool remains a legacy implementation; passing its
codec tests does not make its live registration logic safe.

## Validation

Offline regressions cover restoration/reboot, unsolicited/unrelated/ambiguous
assignments, valid registration and idle responses, stale same/different-channel
queues, read-only transitions, loss of ownership, competing registration requests,
foreign/corrupt/prefixed queue entries, malformed flow control, and a passive
observer alongside a legitimately registered client. New hardening tests failed
before their fixes (9 failures), then passed.

Final offline validation with ESPHome 2026.10.2:

| Check | Result |
| --- | --- |
| C++ protocol suite | 457 checks, zero failures |
| JS serial codec and simulator suites | 43 tests passed |
| ESP32-S3 ESP-IDF firmware compile | Passed in isolated build directory |
| Git whitespace check | Passed |
| Baseline firmware/config and MQTT package | SHA-256 unchanged |
| MQTT diagnostic bounds | 20 top-level fields, max 48 poll entries; conservative 1,268-byte JSON at 30s |

The conflicting-channel regression also failed before its fix (4 failures).
Branch: `fix/bp200g1-coexistence`. The resulting commit and complete diff are in
the local review record; the passive device YAML and unrelated local files are
excluded from the commit.

Final results and build identity are recorded in `.esphome/coexistence-review/`.
Reproduce with the commands in [the procedure](bp200g1-commissioning.md). Build output
uses `/tmp/balboa-bp200g1-build`, not the original passive firmware build directory.

## Remaining interference conditions

[Protocol evidence and ambiguity](channel-ownership.md) are deliberately retained.
A fixed hash shared by another joining client, a delayed matching assignment, or
controller address reuse could still cause authority to be misattributed. Exact
non-echo responses are rejected; some controllers may therefore never register.
An observed conflicting assignment now revokes ownership, but a missed assignment
or identical hash cannot trigger that protection. Fresh registrations can consume
controller channels. No speculative recovery,
random token, ACK-of-ACK, or timing change has been added.

UART buffering may delay poll handling; the code does not timestamp incoming
frames or enforce a measured response deadline. Driver-enable timing, electrical
collisions, controller reset/reallocation, or UART failures can still interfere.
`flush()` results are not checked; software TX counters cannot certify wire delivery.
Button commands retire when the void callback returns and are not retried. Physical
capture and the three evidence gates are required before declaring coexistence.

The answer to “will active mode request registration automatically?” is yes:
valid NEW_CLIENT_CTS triggers the existing request while unregistered. Successful
internal adoption is stronger than receiving an assignment, but controller-level
success needs the timestamped request/matching assignment/ACK trace and subsequent
poll/response evidence. TP500 responsiveness is an independent acceptance condition.
Control is a third gate, after coexistence, with exactly one approved command.
