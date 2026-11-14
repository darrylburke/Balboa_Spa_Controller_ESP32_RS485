# Optional RS485 provenance v1

Include `packages/rs485_events.yaml` alongside a component ID `spa` and MQTT. It
sets only `diagnostic_events: true`. The existing non-retained 30-second topic
`balboa/waveshare/diagnostics/rs485` and its fields are unchanged. The optional
non-retained topic `balboa/waveshare/diagnostics/rs485/events/v1` publishes at most
16 events per 5 seconds, including empty snapshots for clock/liveness observation.

## Time and ordering

Snapshots/events carry `uptime_ms` (ESPHome 64-bit monotonic time), `utc_s` (UTC Unix
seconds or null), boot-scoped random `boot_id`, and event `seq`/`ref`. UTC is accepted
only when system time is in 2020–2106; no clock service is enabled. Unsynchronized
records remain null after later sync; no dates are backfilled. The system clock
can still be inaccurate or jump. Collector UTC receipt time is a different field.
Boot IDs are correlation aids, not identities; sequences/counters wrap at 2^32.
Event timestamps describe local handling, not exact wire arrival/transmission.

There are fixed rings for 64 critical and 16 sampled events. No per-event heap
allocation, logging, MQTT, delay, or disk operation runs in the bus paths. Recorder
storage is reserved even when disabled; it adds a few KiB of RAM. High-rate valid
polls are sampled per channel at most once per 5 seconds; corresponding idle UART
and callback results follow the same sample decision. `polls_coalesced` reports
omissions. `critical_overwritten` and `sample_overwritten` count ring overflow;
`pending` reports backlog. Busy sampled polls cannot evict critical registration
records. This is bounded evidence, not a lossless bus capture. Instrumentation
has a finite CPU cost; hardware response timing remains to be measured.

Events are drained during publication. Broker disconnects, rejected publishes,
QoS/network loss, reset, or overflow can lose evidence; sequence gaps and receiver
receipt gaps matter. Do not pass commissioning gates using an incomplete trace.
The collector must preserve topic, full payload, and receipt UTC, not flatten away
boot/sequence/outcome fields. Existing MQTT status telemetry remains available.

## Event fields

All channels/type/detail/ref values are numeric. Frame type packs the two type
bytes, e.g. `0xBF03`; only the assigned-channel field identifies the channel.

| Kind | Meaning / detail / reference |
| --- | --- |
| RESTORE | Saved unverified address loaded; never TX authority |
| INVITATION | New-client broadcast observed; detail=payload length |
| REQUEST | Gateway request intent; detail=request hash; seq becomes request ref |
| ASSIGNMENT | Candidate observed; detail=response hash or zero if short; ref=pending request |
| REJECTED | detail=RejectReason enum in `events.h`; no evidence of acceptance |
| ACK_RETURN | ID_ACK local callback returned; detail=`cause<<16 | ok`; ref=request |
| ACCEPTED | Internal authority established after successful local ACK result; ref=request |
| OWNERSHIP_LOST | detail=reason; ref=registration request |
| QUEUE_CLEARED | detail=number discarded; ref=reason |
| QUEUED | detail=frame length; seq is local command/frame reference |
| QUEUE_RETIRED | detail=1 after successful local send; CONFLICT reason after panel cancellation; ref=queued seq |
| OTHER_TOGGLE | Observed other-client item toggle; detail=item code; not authenticated TP500 identity |
| QUEUE_EXPIRED | detail=local TTL in ms; ref=queued seq; no retry |
| READY | Sampled observed poll; detail=payload length; seq links idle response |
| WRITE_RETURN | Local callback returned; detail=`cause<<16 | ok`; ref=request/poll/queue |
| UART_RETURN | Actual UART write/flush returned; detail described below; ref=engine frame context |

TxCause: NONE=0, REQUEST=1, ACK=2, IDLE=3, QUEUE=4, SELFTEST=5. UART_RETURN detail:
`cause<<24 | uart_component_failed<<23 | flush_result<<16 | byte_length`.
ESPHome 2026.10.2 flush results: success=0, timeout=1, failed=2, assumed-success=3.
The ESP32 checked callback accepts only success and a healthy UART component.
The final transport also blocks self-test writes in read-only or registration-only mode.
Legacy `set_write_fn` callbacks do not provide this result and must not be treated
as equivalent evidence. `WRITE_RETURN.ok` for those means callback return only.

Snapshot `registered` is internal authority, not proof of exclusive ownership.
`status_valid/config_valid`, nullable `pump1_raw/pump1_capability/heating_mode_raw`,
and nullable `light1` expose observed raw values, not guessed hardware capability.
Queue references are local frame identifiers, not MQTT transaction IDs. No command
confirmation event exists: the protocol has no toggle ACK, and a later matching
status could be a panel/automatic action rather than this gateway's command.

A valid commissioning trace needs REQUEST -> successful UART/WRITE return ->
matching ASSIGNMENT -> successful ACK UART/ACK_RETURN -> ACCEPTED -> subsequent
READY/response evidence. Internal acceptance cannot prove the controller received
ACK. TP500 responsiveness and physical effects remain independent requirements.
