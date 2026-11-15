# Local MQTT retry pacing trial

The reviewed ESPHome 2026.10.2 MQTT component retries pending discovery and entity
state on every main-loop eligibility check. With an async queue full, this can
repeatedly enqueue partial state/discovery sets and debug messages. Production
reported more than 3,000 dropped outbound messages in ten seconds.

`prepare_overlay.py` copies the installed MQTT component to a new local output,
checks the two reviewed source hashes, and changes only failed presentation retry
eligibility. Each component waits one second after a failed resend attempt. New
observations remain in the entity; a later attempt reads its current state.
Healthy change notifications still publish immediately. Incoming commands,
topics, entity schemas, discovery data, MQTT authentication, and RS485 operations
are unchanged. No stale command is retried by this gate.

```sh
../.venv/bin/python support/esphome-mqtt/prepare_overlay.py \
  --source ../.venv/lib/python3.12/site-packages/esphome/components/mqtt \
  --output .esphome/mqtt-paced-retry/overlay
```

Use the prepared `components` directory as a local `external_components` source
with `components: [mqtt]` in a separate trial wrapper. The trial also uses
`mqtt.idf_send_async: true` and limits the MQTT log topic to WARN to avoid filling
the same queue with repeated debug state messages. The installed package and
preserved passive YAML are not edited. The helper has host tests, and the actual
override must compile before deployment. This is not an aggregate-state/HA
migration and does not guarantee MQTT delivery during a network failure.
