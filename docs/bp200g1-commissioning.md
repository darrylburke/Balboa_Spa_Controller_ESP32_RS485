# BP200G1 / TP500 commissioning and recovery

## Current evidence

On 2026-11-15 the operator reported a successful OTA of control candidate v11
(`2026.11.15.25`), confirmed Light OFF at the spa, and reported working remote
commands and normal TP500 operation. The final Jets HA entity is now a switch.

An opt-in saved-channel build v26 (`2026.11.15.26`) was then installed by OTA
for a gateway-only reboot trial. The operator reported that the physical TP500
Light button responded but Jets did not. After OTA rollback to v11 with fresh
registration, the operator reported both physical buttons responding. This
comparison supports the concern that resumed-channel operation can interfere
with panel control on this installation, but does not establish cause without
wire evidence. A 15-minute read-only MQTT observer spanning both uploads received
no device state, availability, or RS-485 event telemetry; the OTA port remained
reachable. Two MQTT command messages were observed, but no device state followed.
Neither the v26 resume event nor the current v11 runtime channel was independently
observed.

Normal Operation and Clear Notification button requests were previously followed
by brief MQTT `offline` messages and recovery; the cause is unknown. No
direction-aware RS-485 capture is attached to this PR, so operator reports do not
prove registration ownership, controller receipt, or repeatable coexistence. See
[`PROJECT_STATE.md`](../PROJECT_STATE.md) for the handoff.

The gateway was returned to v11 (`2026.11.15.25`) via OTA. Keep saved-channel
resume disabled on this BP200G1/TP500 installation unless a future test explains
the Jets failure and verifies ownership on the bus.

## Before any approved hardware work

Keep the local `balboa-passive.yaml` unchanged: `read_only: true`,
`uart_selftest: false`, TX17, RX18, EN21, 115200 baud. It and its rollback copies,
secrets, and firmware images are not in Git. Verify the saved rollback hashes
before relying on them:

```sh
(cd .esphome/passive-baseline && shasum -a 256 -c SHA256SUMS)
cmp balboa-passive.yaml .esphome/passive-baseline/balboa-passive.yaml
```

Build only into a separate path using `ESPHOME_BUILD_PATH`; never overwrite the
baseline. Compilation and host tests do not establish safe bus coexistence.

## Evidence gates

1. **Registration:** obtain a direction-aware trace of gateway request, matching
   full assignment, local ACK write result, and subsequent registered polls. A
   saved channel, `registered` flag, nonzero TX counter, or READY match alone is
   insufficient. Same-hash clients and delayed responses remain ambiguous; see
   [channel ownership](channel-ownership.md) and [event provenance](rs485-provenance.md).
2. **Coexistence:** observe at least five minutes of attributed traffic and normal
   TP500 behavior. Preserve the raw trace, gaps, and overflow counts. MQTT snapshots
   alone cannot identify the transmitter. Stop if the panel regresses, traffic is
   duplicated, or ownership is ambiguous.
3. **Controls:** test one approved action at a time and record command, observed
   state, panel behavior, and physical effect. No automatic retry: these are
   non-idempotent toggles without a command ACK. The reported Light OFF is evidence
   of one effect, not the full registration/coexistence gate. Verify the final Jets
   switch mapping separately before relying on it.
4. **MQTT/HA:** verify one retained `Spa Jets` switch discovery config, no old fan
   or `Spa Jets Power` config, plain per-entity state, and current availability.
   The recent trial showed offline transitions after button commands; investigate
   availability retention before calling HA uptime reliable. See the
   [MQTT schema](mqtt-command-state-schema.md).

## Next steps

- Keep fresh correlated registration enabled; do not retry saved-channel resume
  on this installation yet.
- Capture gateway and TP500 direction-aware traffic during a simple panel Light
  and Jets test. Include channel assignments, READY polls, and replies so the
  resumed-channel failure can be attributed rather than inferred from state.
- Restore MQTT state/availability telemetry and verify the `POSSIBLE_CHANNEL_LEAK`
  warning on a fresh assignment that replaces a saved address. The protocol can
  identify a possible leak, not prove that the old slot remains allocated.

Do not infer pump speed capability or Ready-in-Rest behavior from the historical
CNBP501X profile. Do not implement unattended reconciliation until command timing
and attribution are measured; see [command lifecycle](command-lifecycle.md).

## Recovery

Agree the recovery device and transport before an approved active stage. Restoring
source does not stop firmware already running on the ESP32. If rollback is needed,
verify the saved image hashes and use only the previously confirmed device address.
These commands flash; run them only under explicit operator authorization:

```sh
# OTA rollback; verify the saved hash first.
esphome upload balboa-passive.yaml --device "$BALBOA_ROLLBACK_DEVICE" \
  --file .esphome/passive-baseline/firmware.ota.bin

# USB factory-image recovery; use only the verified Waveshare serial port.
esphome upload balboa-passive.yaml --device "$BALBOA_USB_PORT" \
  --file .esphome/passive-baseline/firmware.factory.bin
```

Do not erase NVS, guess a serial port, or use the OTA image for USB factory
recovery. After recovery, verify passive flags, zero TX counters, fresh telemetry,
and working TP500 controls. See [transport limits](transport-liveness.md).
