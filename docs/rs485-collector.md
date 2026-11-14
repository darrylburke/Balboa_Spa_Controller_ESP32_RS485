# Diagnostics collector integration (not deployed)

Repository audit found no EC2 collector/service definition, persistence query, or
recorded persisted RS485 diagnostics. `spa-mqtt-tester` subscribes to the base topic
but is an interactive control tool, not a durable collector. Receipt and persistence
on EC2 are UNKNOWN. No SSH, service inspection, broker subscription, infrastructure
change, deployment, or credentials access was performed in this task.

Minimal proposed change to an existing approved collector:

1. Add exact receive-only subscriptions to
   `balboa/waveshare/diagnostics/rs485` and
   `balboa/waveshare/diagnostics/rs485/events/v1`.
2. Persist full JSON plus topic and a collector UTC receipt timestamp. Preserve
   device UTC nulls, uptime, boot ID, sequence/ref, and loss counters. MQTT delivery
   duplicates can occur: deduplicate using device/boot/seq/uptime, not UTC alone.
3. Reuse existing protected authentication and TLS/network policy; do not print
   credentials, put passwords in command-line arguments, or change listeners/ACLs.
4. Add rotation/retention and disk-space monitoring through a separately approved
   deployment. Receipt alone does not establish durable persistence; query an
   actual saved record after deployment and record its storage location.

`tools/rs485_collect.py` is a tested, standard-library stdin adapter, not a service
and not a network client. It accepts topic + space + JSON lines (e.g. approved
subscriber output format `%t %p`) for those exact topics, bounds input lines at
32 KiB, and appends NDJSON with `received_utc`. New files use mode 0600. Arrange
rotation externally; this script does not bound total disk retention. It never
publishes or touches spa state. Do not use the interactive MQTT tester as collector.

Offline replay example (no broker connection):

```sh
printf '%s\n' 'balboa/waveshare/diagnostics/rs485/events/v1 {"schema_version":1,"utc_s":null,"events":[]}' | python3 tools/rs485_collect.py /tmp/rs485-replay.ndjson
python3 -m unittest discover -s tests -p 'test_*.py'
```

The real collector integration and proof of persistence remain an operator-authorized
live task. The adapter is supplied for review; it has not been installed on EC2.
