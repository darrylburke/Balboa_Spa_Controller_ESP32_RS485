# Spa Android App — Design

Date: 2026-09-25
Status: Draft for review

## 1. Purpose

A native Android app for one household to monitor and control its Canadian Spa Co.
Cambridge hot tub (Balboa CNBP501X pack, firmware V36.0; accessories pump 1 two-speed
and light 1) from anywhere, through the existing ESP32-C3 RS-485 → MQTT gateway
(`balboa-esp32-mqtt`) on the household MQTT broker.

Success means: from the phone you can see live water temperature and state, change
the target temperature, jets, light, heat mode, range, filter cycle 1 and hold, and
trust that what the app shows is what the spa is actually doing — or be told
plainly that it can't know.

Not a goal: multiple spas, user accounts, the SaaS product
(`spa-controller-saas`), or any change to the gateway firmware.

## 2. Decisions

| Decision | Choice |
|---|---|
| Audience | Just this household, one spa |
| Visual style | "B · Modern thermostat": dark Material 3, temperature dial, tiles, segmented selectors |
| Build approach | New standalone Kotlin/Compose app |
| Transport | Direct MQTT over TLS to the broker (port 8883); optional pinned private CA |
| Broker account | Dedicated app account, ACL `spa/#` only (not the gateway's account) |

## 3. Project

- Location: `SPA/spa-android/`, its own git repository.
- Package: `ai.northtrail.spa`. Min SDK 26, target SDK 37.
- Stack (mirrors the Jandy app): Kotlin, Jetpack Compose + Material 3 (Compose BOM
  2026.08.00), lifecycle-viewmodel-compose, HiveMQ MQTT client 1.3.3, Gradle wrapper.
- Build: `./gradlew testDebugUnitTest assembleDebug`; install with
  `adb install -r app/build/outputs/apk/debug/app-debug.apk`.

## 4. Architecture

| Unit | Responsibility | Origin |
|---|---|---|
| `ConfigStore` | Broker host, port, username, topic root; password encrypted with an Android Keystore AES-GCM key, never logged or re-displayed | Copied from Jandy app |
| `MqttRepository` | TLS connect with pinned CA + hostname verification; subscribe `spa/#`; publish; auto-reconnect; exposes connection state and a message flow | Adapted from Jandy app |
| `SpaTopics` | The single table of every state and command topic (section 5) | New |
| `SpaStateReducer` | Pure `(SpaState, topic, payload, receivedAt, retained) -> SpaState` | New |
| `SpaCommands` | Pure `SpaAction -> List<Publish(topic, payload)>` | New |
| `Temperature` | °C⇄°F conversion and command encoding matching the firmware's rounding | New |
| `SpaViewModel` | Wires repository → reducer → `StateFlow<UiState>`; debounce; pending-command tracking; staleness clock | New |
| UI | Spa, Filters, Settings tabs + unreachable state (section 6) | New |

Data flow: broker → `MqttRepository` → `SpaStateReducer` → `StateFlow` → Compose.
User input → `SpaViewModel` → `SpaCommands` → `MqttRepository.publish`.

Pure units (`SpaTopics`, `SpaStateReducer`, `SpaCommands`, `Temperature`) have no
Android dependencies and are unit-tested on the JVM.

## 5. MQTT surface (verified against live discovery, 2026-09-25)

Topic root `spa`. All temperatures on the wire are **°C**.

State topics (subscribe):

| Field | Topic | Payload |
|---|---|---|
| Gateway online | `spa/status` | `online` / `offline` (LWT) |
| Bus connected | `spa/binary_sensor/spa_bus_connected/state` | `ON` / `OFF` |
| Current temp | `spa/sensor/spa_current_temperature/state` | °C float, or `nan` before first Status |
| Target temp | `spa/sensor/spa_target_temperature/state` | °C float |
| Heating | `spa/binary_sensor/spa_heating/state` | `ON` / `OFF` |
| Jets on | `spa/fan/spa_jets/state` | `ON` / `OFF` |
| Jets speed | `spa/fan/spa_jets/speed_level/state` | `1` / `2` (reads `1` while off; only meaningful when state is `ON`) |
| Light | `spa/switch/spa_light/state` | `ON` / `OFF` |
| Hold | `spa/switch/spa_hold/state` | `ON` / `OFF` |
| Heat mode | `spa/select/spa_heating_mode/state` | e.g. `ready`, `rest` |
| Range | `spa/select/spa_temperature_range/state` | `high` / `low` |
| Spa scale | `spa/select/spa_temperature_scale/state` | `fahrenheit` / `celsius` |
| Priming | `spa/binary_sensor/spa_priming/state` | `ON` / `OFF` |
| Filter 1 running | `spa/binary_sensor/spa_filter_cycle_1_running/state` | `ON` / `OFF` |
| Filter 1 start hour / duration | `spa/number/spa_filter_1_start_hour/state`, `spa/number/spa_filter_1_duration/state` | integer (hour 0–23; minutes 0–1439) |
| Model / firmware / notification | `spa/sensor/spa_model/state`, `spa/sensor/spa_firmware_version/state`, `spa/sensor/spa_notification/state` | text (ESPHome publishes text sensors under `sensor/`) |

Filter cycle 2 is not published by the gateway at all (neither settings nor running
state), so the app does not show it.

Command topics (publish):

| Action | Topic | Payload |
|---|---|---|
| Set target | `spa/climate/spa/target_temperature/command` | °C, one decimal |
| Jets off | `spa/fan/spa_jets/command` | `OFF` |
| Jets low / high | `spa/fan/spa_jets/speed_level/command` | `1` / `2` (the firmware turns the pump on at that speed) |
| Light | `spa/switch/spa_light/command` | `ON` / `OFF` |
| Hold | `spa/switch/spa_hold/command` | `ON` / `OFF` |
| Heat mode | `spa/select/spa_heating_mode/command` | option string |
| Range | `spa/select/spa_temperature_range/command` | `high` / `low` |
| Filter 1 | `spa/number/spa_filter_1_start_hour/command`, `.../spa_filter_1_duration/command` | integer |
| Clear notification | `spa/button/spa_clear_notification/command` | `PRESS` |

Select options (from the firmware): heating mode `ready`/`rest` (Ready-in-Rest reports
as `ready`), range `high`/`low`, scale `fahrenheit`/`celsius`.

## 6. Screens

Bottom navigation with three tabs. A connection strip sits at the top of Spa and
Filters.

1. **Spa (home)** — connection strip ("● Spa connected · 2s ago"); temperature dial
   showing current temperature, "Heating to N°" / "Idle", and range; −/+ around
   "Set N°F"; tiles: Jets (tap cycles Off → Low → High) and Light (tap toggles);
   segmented selectors for Heat mode (Ready/Rest) and Range (High/Low).
2. **Filters & hold** — Filter cycle 1: start hour and duration (15-minute steps),
   running indicator, "Save to spa". Hold: state and "Start/End hold".
3. **Settings & status** — Connection (broker connected, gateway online, spa bus
   connected, broker host, "Edit broker / password"); Spa (model, firmware, display
   units °F/°C/Follow spa); Notification (current spa message, "Clear
   notification").
4. **Unreachable state** — red strip naming the failing link and the age of the
   data; last-known values greyed; controls disabled; short plain-language reason.

First run shows the broker setup screen (host, username and password entered once;
port 8883 and topic root `spa` prefilled).

## 7. Behaviour

### 7.1 Liveness

Three independent signals: broker connection (HiveMQ client state), gateway
(`spa/status`), spa bus (`spa_bus_connected`). Controls are enabled only when all
three are good; the strip names the first failing one.

The gateway publishes only on change plus a 60-second refresh, so data is **stale**
only after **90 seconds** without any live `spa/` message, measured from the later of
the last live message and the moment the app connected (so opening the app does not
immediately show "stale"). Stale data is shown greyed with its age.

Retained messages delivered at subscribe time are marked "last known" until a
live (non-retained) update arrives for that field.

### 7.2 Commands

- Target temperature: −/+ adjust a local draft; the command is sent **1 second after
  the last tap**. Draft clamped to the range limits (section 7.3).
- Every command enters a **pending** state shown on its control. It resolves when
  the spa's state topic echoes the requested value. If no echo arrives within
  **10 seconds**, the control reverts to the spa's reported value and a snackbar
  says "Spa didn't confirm".
- Hold and "Save to spa" (filters) ask for confirmation first.
- Commands are never sent while controls are disabled.

### 7.3 Temperature

- Display unit: follows the spa's scale by default; overridable in Settings.
- The firmware maps °C back to the spa's raw whole-°F value. The app encodes a
  whole-°F target as °C to one decimal such that the firmware's conversion yields
  exactly that °F. A unit test covers every whole °F in both ranges.
- Range limits (Balboa standard, **unverified on this spa**): High 80–104 °F,
  Low 50–99 °F. Held in one constant; confirmed against the topside panel during
  the manual test.

## 8. Error handling

| Situation | Behaviour |
|---|---|
| No network / broker unreachable | Strip: "Can't reach broker"; retries with backoff; controls disabled |
| Bad credentials / TLS failure | Strip + Settings error text; no retry storm; prompt to edit broker settings |
| Gateway offline (LWT) | Strip: "Gateway offline · last seen N min"; controls disabled |
| Bus disconnected | Strip: "Gateway can't hear the spa"; controls disabled |
| Command not confirmed in 10 s | Revert + snackbar |
| Unparseable payload | Ignored for that field, logged at debug; never crashes |

## 9. Security

- Dedicated broker account with ACL `spa/#` read/write only; revocable
  without touching the gateway.
- TLS with hostname verification; a private CA can be pinned via
  `assets/broker_ca.pem`, otherwise the system trust store. Plain MQTT not supported.
- Password encrypted with Android Keystore; never logged, committed, or shown again.

## 10. Testing

- **JVM unit tests** (the bulk): `SpaStateReducer` against payloads captured from
  the live broker; `SpaCommands` topic/payload for every action; `Temperature`
  round-trip for every whole °F in both ranges; staleness and retained handling;
  pending-command confirm/timeout; target debounce.
- **One Compose UI test**: unreachable state disables controls.
- **Manual acceptance on the real spa**: target temperature, jets Off/Low/High,
  light, Ready/Rest, High/Low, filter 1 save, hold on/off, clear notification —
  each confirmed in the app and on the topside panel. Also confirm range limits.

## 11. Out of scope

Push notifications/background alerts, home-screen widgets, filter cycle 2 (not
published by the firmware), spa clock display/editing (firmware auto-syncs), multiple spas,
SaaS login, iOS.
