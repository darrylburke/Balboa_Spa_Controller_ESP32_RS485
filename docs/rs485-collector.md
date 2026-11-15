# RS-485 diagnostics collector integration (not deployed)

There is no EC2 collector/service definition or evidence of persisted RS-485
diagnostics in this repository. The active control candidate no longer publishes
the periodic `balboa/waveshare/diagnostics/rs485` topic. Request `STATUS 8` to get
RS-485 counters on `balboa/waveshare/status`; optional bounded event batches remain
on `balboa/waveshare/diagnostics/rs485/events/v1` when an events publisher is
configured. The preserved passive rollback still includes the legacy schema-v1
publisher.

`tools/rs485_collect.py` is a standard-library stdin adapter, not a network client
or service. It accepts exact topic + space + JSON lines (for example, approved
`mosquitto_sub` output `%t %p`) for STATUS 8 responses and events. It also accepts
legacy schema-v1 diagnostic records for historical captures. The adapter bounds
input lines at 32 KiB and appends NDJSON with a collector UTC receipt timestamp.
New files use mode 0600. It does not publish, request STATUS, or touch spa state.

To capture a STATUS 8 response, subscribe to `/status` and issue `STATUS 8` through
an authorized command path. Preserve the full JSON plus topic and receipt UTC; do
not flatten boot IDs, uptime, event sequence/ref, or loss counters. MQTT delivery
duplicates can occur. Deduplicate events using device/boot/seq/uptime, not UTC
alone.

Arrange rotation/retention and disk-space monitoring through a separately
approved deployment. Receipt alone does not establish durable persistence; query
an actual saved record after deployment and record its storage location.

Offline replay example (no broker connection):

```sh
printf '%s\n' 'balboa/waveshare/diagnostics/rs485/events/v1 {"schema_version":1,"utc_s":null,"events":[]}' | python3 tools/rs485_collect.py /tmp/rs485-replay.ndjson
```
