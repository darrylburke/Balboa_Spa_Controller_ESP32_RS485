# UART and main-loop liveness

The BP200G1 production trials observed task-watchdog resets and periods with no
fresh MQTT telemetry. A later retained RTC breadcrumb placed a watchdog outside
the Balboa component's instrumented stages, with no transport fault latched.
That does not identify a specific ESPHome component or prove a UART fault caused
the resets. Absence of telemetry also did not prove absence of registration TX:
a later snapshot showed internal registration and advancing TX counters.

## UART protection

`TransportWrite` checks backend health and capacity for the whole frame before
enabling DE. Unknown capacity (`SIZE_MAX`) and insufficient capacity fail closed.
The installed ESPHome IDF backend supplies this capacity from its TX ring buffer.
The check assumes this component exclusively owns the UART writer; it is not an
atomic reservation against a second writer.

The adapter sets an IDF flush timeout of 100 ms. This is a local liveness limit,
not a measured controller deadline, registration delay, ACK, or retry interval.
After a failed write/drain it releases DE and rejects all further writes for the
rest of the boot. Re-enabling DE could expose bytes still pending in the UART, so
there is no automatic transport recovery or retransmission. The protocol's failed
checked-write path revokes pending/registered authority and invalidates the queue.
Healthy registered poll responses remain mandatory and repeatable.

This is not a hard real-time bound on the entire driver call. Backend capacity
and the driver itself must behave as documented; a driver defect can still hang
before the drain timeout runs. Non-IDF backends do not necessarily implement a
finite flush timeout. Older ESPHome versions are not validated. Host tests cover
full/unknown buffers, failed write/drain, driver release, fault latching, continued
healthy replies, and rejection of a late assignment after a transport fault.

## Watchdog breadcrumbs

Plain `RTC_NOINIT` diagnostic storage records the current execution stage and
last TX type/length. On a watchdog or panic reset the next component setup saves
the previous stage. That saved fault survives subsequent orderly software boots
within the same RTC layout. Power loss, invalid storage, or a changed memory
layout can lose it. It is not a persistent identity, authority, stack trace, or
physical delivery record. A single global trace describes the most recent Balboa
component execution, so multiple component instances cannot be distinguished.

Stages include UART read, parser feed, entity callbacks, TX write/drain, and the
state snapshot publisher. OUTSIDE means none of those stages was active at the
last update; it does not name the external component that stalled. Updates are
fixed memory stores, not per-frame MQTT, logging, flash writes, or allocations.

## MQTT trial

The installed ESPHome IDF MQTT configuration defaults to synchronous sends on the
main loop. The dedicated passive trial enables `mqtt.idf_send_async: true`, which
uses ESPHome's separate sender and bounded outbound queue. Queue pressure can
still drop publications and must remain visible. Early passive observations
showed improved ping delivery and fresh state; this is a mitigation trial, not a
proven reset diagnosis or active coexistence result. The preserved passive YAML
and rollback binaries remain untouched. No Light/Jets/heating command follows
from a successful OTA or an internal registered flag.

## Delayed startup evidence

An active boot did not report fresh MQTT diagnostics until roughly 147 seconds
after startup. A 75-second host cutoff was therefore inadequate. The subsequent
test uses a ten-minute startup allowance, followed by its normal registration and
coexistence checks once live evidence arrives. This is a host observation policy,
not a controller response timing guarantee or permission to hide a failed trace.

The old event YAML also removed records inside a `mqtt.publish_json` payload.
The installed implementation serializes that payload before checking connection
state, so startup event batches were discarded even while disconnected. The new
publisher skips offline batches, snapshots without popping, and consumes a batch
only after local MQTT admission. Host tests cover long offline intervals, rejected
admission, preservation of newer records, and refusal to consume an evicted prefix.
The bounded async queue can still lose an admitted packet on connection loss;
receiver sequence checks and explicit overflow/coalescing counters remain required.
