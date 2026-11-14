# BP200G1 / TP500 commissioning and rollback

Status: offline verification only. No device has been flashed, enabled for active
operation, rewired, or commanded during this engineering task. Each physical stage
below needs separate explicit operator approval. Do not run the legacy serial
bench tool against the connected spa: its ownership rules have not been corrected.

## 0. Preserve and reproduce (offline)

From the repository root, the immutable local rollback copies are in
`.esphome/passive-baseline/`: `balboa-passive.yaml`, `firmware.ota.bin`,
`firmware.factory.bin`, `firmware.elf`, and `SHA256SUMS`. These are the existing
artifacts preserved before this task's build, not new rollback builds. This task
did not read back the deployed ESP32 image; confirm these local artifacts match
the operator's validated deployment before relying on them for recovery. They are
ignored by Git, contain no copied secrets file, and must be retained locally.
Keep the existing `secrets.yaml` available; do not commit or print credentials.

Verify the backup before any approved physical stage:

```sh
(cd .esphome/passive-baseline && shasum -a 256 -c SHA256SUMS)
cmp balboa-passive.yaml .esphome/passive-baseline/balboa-passive.yaml
git branch --show-current
git rev-parse HEAD
make test
node --test spa-serial-tester/protocol.test.js spa-simulator/model.test.js
ESPHOME_BUILD_PATH=/tmp/balboa-bp200g1-build esphome compile balboa-passive.yaml
```

These commands do not flash. The device configuration must remain `read_only: true`
and `uart_selftest: false`. RX=GPIO18, TX=GPIO17, EN=GPIO21, 115200 baud remain unchanged.
Review artifacts and commit identity are recorded locally in
`.esphome/offline-review/`; provenance candidate output is under
`/tmp/balboa-offline-events/waveshare-balboa/build/`, and the unchanged-config
build uses `/tmp/balboa-offline-production/waveshare-balboa/build/`. Preserve needed /tmp artifacts
before clearing temporary files. Do not overwrite the rollback directory.

The latest publication candidate and checksum-guarded future upload/rollback
commands are recorded under `.esphome/status-review/`. See the
[publication audit](status-publication-audit.md) before using the earlier candidate
artifacts. The command scripts are supplied for review and were not executed.

## 1. Passive candidate validation (future, separately approved)

Only after approval, deploy the candidate with both passive settings unchanged.
Do not repurpose the known-good YAML into an active configuration. For at least
five minutes, collect timestamped MQTT diagnostics using an existing approved
read-only subscriber, and compare telemetry with the TP500. Verify:

- `read_only=true`, `uart_selftest=false` in every snapshot.
- `tx_frames=tx_bytes=0`, `channel_adoptions=0`, and receive/poll counters advance.
- Consolidated `balboa/waveshare/state` snapshots arrive around every 10s with
  fresh observations. Measure decoded Jets/Light status -> HA MQTT receipt <=1s
  using a common-clock trace; offline dispatch tests do not establish that bound.
- TP500 controls remain responsive; telemetry continues to match the panel.
- A restored nonzero `channel`, `channel_restores=1`, or growing `ready_ours` does
  not count as ownership. They may simply describe the TP500's channel.

Any nonzero TX in passive mode is a stop/rollback condition. Counter deltas must
account for reboot/uptime resets and uint32 wrap. Keep the deployed old baseline
until this stage is approved; compilation alone is not deployment approval.

## 2. Registration gate (future active test, separate approval required)

Before enabling active mode, approve the test scope, exact candidate artifact,
observation method, rollback transport/address, and operator availability. Do not
change wiring or install a bus analyzer without approval. Existing MQTT snapshots
are insufficient for the required timestamped per-frame evidence. Arrange an
approved receive-only capture method that can distinguish gateway TX from RX;
otherwise this stage cannot pass. The optional [provenance v1](rs485-provenance.md)
provides local event sequence/ref, boot, uptime, UTC when valid, and UART outcome.
Enable it in the separate passive candidate first. Preserve the existing 30-second
snapshot too. Verify the collector actually persists both topics: repository-only
inspection did not establish EC2 persistence. See [the integration plan](rs485-collector.md).

Use the local event chain REQUEST -> successful UART/WRITE_RETURN -> matching
ASSIGNMENT -> successful ACK UART/ACK_RETURN -> ACCEPTED, followed by sampled
READY and idle UART/WRITE_RETURN with the same poll ref. No critical-overwrite,
sequence/network gap or unknown local outcome may be hidden in the acceptance
window. Sample/coalesced counters disclose omitted polls: the events are not a
lossless physical-bus trace. If this evidence is incomplete or ambiguous, an
approved independent receive-only capture is needed before progression. Receipt
UTC cannot replace a null device event timestamp; uptime establishes local order.

An approved active test must use a separate candidate configuration, never edit
the preserved passive file. No remote control commands are allowed at this gate.
Use `packages/rs485_registration_only.yaml` in that separate candidate and verify
`registration_only=true` in provenance snapshots. It blocks application commands,
automatic configuration requests and time-sync writes while retaining the existing
registration/mandatory idle replies. It never enables active mode itself. The
current offline provenance wrapper is still read-only; no active candidate has
been enabled or flashed. UART self-test remains disabled. Audit configured
application automations and obtain separate approval before any active build/deploy.

The corrected gateway starts unregistered, despite a restored address. It
**automatically requests registration on each valid NEW_CLIENT_CTS while it remains
unregistered**. There is no immediate boot-time write and no MQTT trigger is needed.
Record monotonic timestamps, direction/source evidence, and channel identifiers for:

1. Controller `FE BF 00` (NEW_CLIENT_CTS), followed by gateway
   `FE BF 01 02 F1 73` (ID_REQUEST).
2. Controller `FE BF 02 <assigned-channel> F1 73`, with channel `10`–`2F` and a
   pending gateway request. Reject a shortened or mismatched response as success.
3. Gateway `<assigned-channel> BF 03` (ID_ACK) immediately in response, using the
   existing exchange. Do not expect an invented ACK-of-ACK.

`assignments` only counts received broadcasts; `last_assignment` is observation.
`channel_adoptions` increments after correlation and a successful checked ACK callback;
the assignment persistence log has the same limitation. `tx_frames/tx_bytes`
indicate UART write/flush call returns, not confirmed controller receipt. They do
not establish exclusive ownership, and may include repeated requests/configuration.
Do not infer a successful exchange from a nonzero channel or a single counter.

A distinguishable competing request cancels the pending exchange; the next
NEW_CLIENT_CTS can retry. Same-hash clients and delayed matching responses remain
ambiguous. If capture cannot associate this exchange with the gateway rather than
the TP500, stop. Do not repeatedly reboot to force registration or exhaust channels. For the
approved test, stop after five valid invitations without a complete registration
trace; this is an operator test limit, not a new firmware timeout or protocol rule.

## 3. Coexistence gate (only after registration evidence passes)

Observe subsequent controller READYs and corresponding gateway queued/idle
responses on the assigned channel. Capture timestamps, channel, type, and direction.
Confirm that the gateway never answers another client's poll. Preserve the raw
capture alongside a short event table:

| Time | Direction/device evidence | Channel | Type | Interpretation |
| --- | --- | --- | --- | --- |
| measured | controller/gateway/analyzer evidence | measured | measured | request/assignment/ACK/READY/response |

Observe at least five minutes and include approved operator TP500 button presses.
The TP500 must remain responsive, with each expected physical effect and reported
state visible; telemetry and MQTT connectivity must remain stable. No remote
commands yet. Record baseline and candidate poll/status rates and response latency;
use measured frame order, not a guessed new timing threshold.

A READY on the restored or assigned channel alone is not a pass: it may predate the
gateway and belong to another device. Detect duplicate responses, CRC errors,
missing/late responses, unexplained channel changes, and any TP500 regression.
Any such result stops progression and triggers the approved rollback plan. Do not
weaken correlation, hardcode `0x10`, or adjust timing to make this gate appear to pass.

## 4. Light control gate (only after coexistence passes and command approval)

First obtain separate approval for a control-capable candidate with the
registration-only guard removed, self-test still false, and the same provenance.
Removing that guard also permits automatic configuration queries and any configured
time sync; include these in the approval and audit before that deployment.
Do not edit the preserved passive file. Issue exactly one approved Light command
using its existing MQTT topic, without a retained command or automatic retry. Record its MQTT topic/payload and time,
its outbound frame in the next assigned polling opportunity, the physical effect,
and subsequent reported state. Check TP500 responsiveness again. No automatic
command retries: toggle requests have no delivery acknowledgement and retrying may
reverse a successful effect. A callback return or increased TX counter is not proof
that the spa executed the command.

Only after Light passes, propose separate Jets and heating-mode tests. Capture
raw capability/status and approved physical panel cycles before choosing single-
or two-speed assumptions or interpreting READY_IN_REST. [Command limits](command-lifecycle.md)
apply: no unattended reconciliation or reassertion is implemented. Stop on repeated
or unexplained commands, unexpected message causes, a queue that outlives its TTL,
unknown/ambiguous authority, missing UART outcomes, or any TP500 nonresponsiveness.

Do not test ownership-loss/reboot/power-cycle scenarios on the physical spa without
additional approval. They are covered offline and can consume controller channels.

## Rollback (physical action; execute only when explicitly authorized)

Agree the recovery transport and device address before the active test. Restoring
source files alone does not stop already-running firmware from transmitting.
If interference occurs, stop command producers and use the preapproved means to
isolate gateway TX, then restore the exact saved passive image. Isolation/rewiring
and flashing are physical actions and were not performed in this task.

For an approved OTA rollback, verify hashes first, set `BALBOA_ROLLBACK_DEVICE` to
the previously confirmed gateway address, and upload the saved image explicitly:

```sh
(cd .esphome/passive-baseline && shasum -a 256 -c SHA256SUMS)
# FUTURE APPROVED ACTION ONLY; this command flashes the device:
esphome upload balboa-passive.yaml --device "$BALBOA_ROLLBACK_DEVICE" --file .esphome/passive-baseline/firmware.ota.bin
```

If OTA is unavailable, do not keep issuing spa commands or repeatedly reboot the
spa. Use separately approved local USB recovery; select the actual enumerated
Waveshare serial port and its verified BOOT/reset sequence. After hash verification,
ESPHome 2026.10.2's serial `--file` path flashes an explicitly supplied factory
image at the factory-image base (verified in the installed uploader source):

```sh
# FUTURE APPROVED USB ACTION ONLY; this flashes and reboots the Waveshare:
esphome upload balboa-passive.yaml --device "$BALBOA_USB_PORT" --file .esphome/passive-baseline/firmware.factory.bin
```

Do not use the OTA-only image for this factory-image command, guess a serial port,
or erase NVS. USB/power isolation and recovery are physical actions needing approval. An OTA
rollback does not reset controller allocations or necessarily restore preference
contents. After recovery verify both TX counters stay zero, telemetry works, and
TP500 controls work. Keep the gateway passive; no spa power cycle is implied or
authorized by this procedure.
