# Gateway channel ownership

A persisted address is observation, not ownership. `registered()` now requires a
fresh registration exchange. This prevents a saved topside address from causing
gateway idle replies or queued commands in the topside's polling windows.

## Source and ambiguity

The [upstream protocol wiki](https://github.com/ccutrer/balboa_worldwide_app/wiki#channel-assignment-response)
describes channel plus two request correlation bytes in an assignment. It records
a non-echo exception and uncertainty about simultaneous joining and channel reuse.
The [cribskip helper](https://github.com/cribskip/esp8266_spa/blob/master/src/balboa_helper.ino)
provides the existing request and ACK encodings.

This correction retains the request bytes `02 F1 73`. It accepts only a full
three-byte response with `F1 73` after its own request callback has returned,
and only polling channels `10`–`2F`. Short, mismatched, and out-of-range responses
are not adopted or ACKed. In particular, `F1 93` is not guessed to mean `F1 73`.

This is correlation, not authentication. Another client using the same hash, or
a delayed matching response, cannot be distinguished. No random identity scheme,
registration timeout, collision backoff, or saved-channel verification message
has been invented. Pending state is cleared by read-only mode, restoration,
ownership invalidation, callback replacement, a distinguishable competing request,
or successful registration; another
NEW_CLIENT_CTS makes a new request using the unchanged bytes. A controller returning
nonmatching bytes may never register this gateway. A future compatibility change
needs controller-specific evidence before relaxing this check.

## Reboot tradeoff

The earlier implementation explicitly resumed the saved channel. Commit
[`75c6055`](https://github.com/darrylburke/Balboa_Spa_Controller_ESP32_RS485/commit/75c6055)
records hardware verification that the controller kept polling that slot after
a gateway reboot. It also records five leaked slots increasing READY response
latency from 1.19 ms to 14.17 ms. That evidence is not refuted by this change.

This change chooses fresh correlated registration because a saved channel alone
cannot establish that the controller has not reset and reassigned that slot.
The cost is real: after a gateway-only reboot, the controller may continue
polling the old slot while fresh registration consumes another one. Repeated
gateway-only reboots can exhaust the 32 slots (`10`–`2F`). The change therefore
does **not** prevent channel leaks; it trades that risk against transmitting
under stale ownership after a controller reset. The protocol provides no
identity proof that resolves both cases. A controller-specific resume rule
needs evidence covering both gateway-only and spa/controller power cycles.

## Queue and polling rules

- Passive/unregistered commands and automatic configuration requests cannot queue
  transmissions for later registration.
- Restoration, entering read-only mode, registered-channel staleness, and accepting
  an assignment invalidate queued frames and reset configuration retry bookkeeping.
  Received telemetry/configuration remains available.
- The public queue also rejects incomplete, corrupt, prefixed, or foreign-channel
  frames, including external frames encoded under a previous channel.
- Only payload-free NEW_CLIENT_CTS/READY messages can authorize writes. Diagnostics
  still count received type matches as before; the JSON schema is unchanged.
- A full assignment of the owned address to different hash bytes revokes
  authority immediately and clears the queue; matching READYs cannot keep it alive.
  The address remains observable; `channel_stale` still counts poll-timeout releases.
- After registration, normal matching polls still send one queued frame, or the
  existing nothing-to-send response. Other channels receive no response.
- Required handshake writes are the exceptions to poll-only transmission:
  ID_REQUEST follows NEW_CLIENT_CTS, ID_ACK follows a correlated assignment.
- Queue retirement means a successful checked callback (or legacy void callback
  return), not controller acceptance. Command retries and UART completion semantics are unchanged.

The diagnostic JSON schema and receive counters are unchanged. `channel` can be a
restored, unowned address and `ready_ours` still means an address match, not proof of
ownership. Passive mode must show zero `channel_adoptions` and zero TX. Saved
addresses are loaded and logged without granting ownership or rewriting storage.

Keep the local device at `read_only: true`, `uart_selftest: false`. The optional
UART self-test must stay disabled; the final UART guard now blocks it in read-only
mode as well. Passing
host tests and compilation does not authorize flashing or active operation.


See [the commissioning and rollback procedure](bp200g1-commissioning.md) for the
separate registration, coexistence, and control evidence gates. The engineering
review and offline results are recorded in [the report](bp200g1-engineering-report.md).
