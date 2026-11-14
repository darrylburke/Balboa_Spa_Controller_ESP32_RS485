# Offline readiness checklist and patch dependencies

Current work branch: `engineering/offline-commissioning`, based on `187887c` from
`fix/bp200g1-coexistence`. The older `fix/rs485-channel-ownership` reference is not
this checkout's full base. Exact commits, patches, hashes and logs are in
`.esphome/offline-review/record.txt` and `.esphome/offline-review/patches/`.

| Requirement | Status | Evidence / limit |
| --- | --- | --- |
| Protect baseline artifacts/config, pins and baud | DONE | SHA-256 before/after; no changes to local device YAML |
| Audit all UART paths and restored identity | DONE | Separate authority, correlation, invalidation, final read-only guard |
| Queue handshake/flow-control injection | DONE | Failing regression then rejection; config-request compatibility retained |
| Transport removal and failed ACK/command handling | DONE | Checked callback, flush/component outcomes; no retry |
| Mandatory legitimate poll responses | DONE | Host tests for handshake, command and idle paths |
| Contention and stale restoration/channel/queue cases | DONE | Host regressions; not hardware proof |
| UTC provenance, ordering and bounded event storage | DONE | Optional v1 topic, UTC null pre-sync, fixed rings, explicit losses |
| Keep existing 30-second MQTT snapshot | DONE | Existing package hash unchanged |
| Establish EC2 persistence | OUTSTANDING | No collector source/config or saved record supplied; live state UNKNOWN |
| Supply non-invasive collector integration | DONE | Undeployed stdin adapter, tests, subscription/storage plan |
| Bounded stale-frame policy and observed panel priority | DONE | TTL, same-item cancellation, no retries/reassertion |
| Remove tester duplicate Jets command / stale verification | DONE | Pure JS planner/observation tests; no live connection |
| Stop optimistic filter state from claiming command success | DONE | Observation-only state, regression and firmware compile |
| Reliable desired-state reconciliation across missed actions | DEFERRED | Needs physical latency/cycle/attribution evidence; no safe inference |
| Discover BP200G1 single/two-speed capability | DEFERRED | Historical profile is not evidence; optional snapshot exposes raw observations |
| Correct READY_IN_REST physical mapping | DEFERRED | Sparse decode known, actual transitions unverified |
| Registration-only commissioning guard | DONE | Blocks app/config/time-sync writes, retains required idle replies |
| Staged procedure and OTA/USB recovery route | DONE | Procedure prepared; no physical stage executed |
| Physical passive candidate / active coexistence / Light effect | OUTSTANDING | Each requires explicit physical authorization |
| Treat persisted 0x10/READY as ownership | OBSOLETE | This assumption caused unsafe reuse; never reintroduce |
| Claim passing tests prove the spa is fixed | OBSOLETE | No physical coexistence or command reliability result yet |
| EDF/tariffs/scheduling optimization | DEFERRED | Explicitly outside scope |

## Reviewable patch sequence

1. Ownership audit: public-queue flow-control rejection and callback-removal safety.
2. Optional diagnostics and transport outcome: event recorder, UTC/ordering, UART
   checked result and final read-only guard. Legacy void callbacks remain supported
   with weaker outcome knowledge. The event package is opt-in, uses new topic v1,
   and enables no clock service or active operation.
3. Command lifecycle/commissioning guard: local TTL, observed same-item cancellation,
   registration-only mode, observation-only filter state, independently tested
   MQTT tester planner. Depends on diagnostics for time/event references.
4. Readiness/docs/collector adapter: no service deployment. The adapter has no
   network publisher or MQTT dependency; live collector integration is separate.

The older `187887c` groups initial diagnostics/ownership fixes. Its original patch
is retained in `.esphome/coexistence-review/`; this series is incremental, not a
claim that the old combined commit has been rewritten into independent PRs.

## Reproduction (offline only)

```sh
(cd .esphome/passive-baseline && shasum -a 256 -c SHA256SUMS)
make test
node --test spa-serial-tester/protocol.test.js spa-simulator/model.test.js spa-mqtt-tester/command-plan.test.js
python3 -m unittest discover -s tests -p 'test_*.py'
ESPHOME_BUILD_PATH=/tmp/balboa-offline-production esphome compile balboa-passive.yaml
ESPHOME_BUILD_PATH=/tmp/balboa-offline-events esphome compile .esphome/offline-provenance.yaml
```

The ignored provenance wrapper includes the untouched local baseline plus
`packages/rs485_events.yaml` and `packages/rs485_registration_only.yaml`. It stays
read-only with self-test false. Review record includes exact wrapper content and
binary hashes. Do not run `esphome run`, `upload`, serial tester, or interactive
MQTT tester during offline validation. ESPHome shared-cache access may require
sandbox permission; it does not authorize deployment or bus access.


## Final verification and compatibility

Final combined suite: 550 C++ checks / zero failures; 46 JS tests passed (43 existing,
3 new pure tester tests); 5 Python collector tests passed. Ownership-only and
provenance-stage C++ snapshots separately passed 463 and 499 checks. Both unchanged-
config and optional provenance/registration-only passive firmware compiled with
ESPHome 2026.10.2 in isolated directories. Baseline and original snapshot hashes
remain unchanged. New regressions failed before their fixes: 5 ownership-audit
checks, 3 expiry/panel-priority checks, one optimistic-observation check and one
malformed-command check. Event-buffer tests cover disabled mode, UTC-null ordering,
overflow separation and uptime rollover; these are host results, not wire timing.

The new optional APIs target the installed ESPHome 2026.10.2 `millis_64()` and
`UARTFlushResult` interfaces. UART platforms that cannot report confirmed flush
success fail closed through the checked path; older ESPHome/platform compatibility
is not claimed. Legacy void protocol-library write callbacks remain source-compatible
but expose only callback-return evidence. Actual physical timing has not been measured.

The event batch has a conservative 3,618-byte compact-JSON bound (16 events/5s);
64 critical + 16 sampled records reserve fixed storage. Existing 30-second JSON
remains at its previously checked 20 fields and bounded channel map. Event loss,
sampling, unsynchronized UTC and unavailable collector persistence are explicit,
not hidden as a claimed complete trace.
