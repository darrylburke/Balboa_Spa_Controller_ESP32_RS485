# Setup & Configuration

How the firmware is configured for **WiFi**, **MQTT**, and the **Balboa controller / accessories**.
All configuration lives in two places:

- **`balboa-spa.yaml`** — the device config (structure; references secrets by name).
- **`secrets.yaml`** — your private credentials (you create this; it is git-ignored and never committed).

> First step: copy the template and fill in your values.
> ```sh
> cp secrets.yaml.example secrets.yaml
> # then edit secrets.yaml
> ```

After editing, flash with `esphome run balboa-spa.yaml`. For the staged bring-up
(read-only first, then enabling control) see [`docs/bring-up.md`](docs/bring-up.md).

---

## 1. WiFi

Defined in `balboa-spa.yaml`:

```yaml
wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  ap:                              # fallback hotspot if the WiFi join fails
    ssid: "Balboa-Spa Fallback"
    password: !secret ap_password

captive_portal:                    # serves a WiFi-setup page on the fallback AP
```

The actual SSID/password are **not** in `balboa-spa.yaml` — they come from `secrets.yaml`:

```yaml
wifi_ssid: "YourWiFi"
wifi_password: "YourWiFiPassword"
ap_password: "fallback-ap-password"
```

**To configure:** set `wifi_ssid` / `wifi_password` in `secrets.yaml` to your network.

**Fallback behavior:** if the ESP32 cannot join your network, it starts its own
access point named **`Balboa-Spa Fallback`** (password = `ap_password`). Connect a
phone to it and the captive portal lets you re-enter WiFi credentials without
re-flashing. Credentials are compiled into the firmware (standard ESPHome model);
changing them later means editing `secrets.yaml` and re-flashing (OTA is fine).

---

## 2. MQTT

Defined in `balboa-spa.yaml`:

```yaml
mqtt:
  broker: !secret mqtt_broker       # host/IP of your MQTT broker
  username: !secret mqtt_username
  password: !secret mqtt_password
  topic_prefix: spa
  discovery: true                   # Home Assistant auto-discovery ON
```

Credentials/host come from `secrets.yaml`:

```yaml
mqtt_broker: "192.168.1.10"
mqtt_username: "spa"
mqtt_password: "YourMqttPassword"
```

**Host / login:** set `mqtt_broker` (IP or hostname), `mqtt_username`, and
`mqtt_password` in `secrets.yaml`.

**Topics:** everything is published under the `spa/` prefix:

| Purpose | Topic pattern |
|---|---|
| Entity state | `spa/<platform>/<object_id>/state` |
| Entity command | `spa/<platform>/<object_id>/command` |
| Device availability | `spa/status` (`online` / `offline`) |

`<object_id>` is derived from the entity name (e.g. `Spa Current Temperature` →
`spa_current_temperature`).

**Home Assistant:** because `discovery: true`, the firmware **also** publishes HA
discovery configs under `homeassistant/…`, so the spa appears in Home Assistant
automatically as a climate device plus switches/sensors — no manual entity setup.
If you do **not** use Home Assistant, set `discovery: false` and consume the
plain `spa/…` topics directly (Node-RED, custom dashboards, etc.).

> Tip: avoid setting `mqtt_username` to the literal `spa`. It works, but because it
> matches `topic_prefix: spa`, ESPHome's `esphome config` output redacts the word
> `spa` everywhere (a cosmetic validator artifact only).

---

## 3. Balboa controller & available accessories (pumps, lights, etc.)

There are **two layers** to how the spa's hardware is handled.

### (a) Runtime auto-detection — informational

On boot, the firmware queries the spa for its configuration and **logs what is
physically present**. Watch the logs (USB, web UI at `http://<device-ip>/`, or MQTT)
for lines like:

```
[balboa_spa] Detected spa: model='...' version='...'
[balboa_spa]   pump1: 2-speed
[balboa_spa]   light1 present
```

The engine also uses this detected configuration to **bound commands** (e.g.
`set_pump` will never drive a pump past its detected maximum speed). However,
auto-detection does **not** create the entities you see in Home Assistant.

### (b) The entities you see — `packages/spa_full_superset.yaml` (static)

ESPHome entities are defined at compile time, so they are declared in
`packages/spa_full_superset.yaml`. This file is the **single place you adjust which
accessories are exposed**. It ships configured for the target spa
(**Canadian Spa Co. Cambridge, Balboa pack — 1× 2-speed pump, 1× light**):

```yaml
fan:
  - platform: balboa_spa
    type: pump
    index: 1            # 1-based (pump 1)
    speed_count: 2      # 2-speed (low / high)
    name: "Spa Jets"

switch:
  - platform: balboa_spa
    type: light
    index: 1
    name: "Spa Light"
```

Also declared (present on this spa): heating mode / temperature range /
temperature scale (`select`), current & target temperature (`sensor`),
heating / priming / filter-cycle status (`binary_sensor`), model / version /
notification (`text_sensor`), filter-cycle start & duration (`number`), and
clear-notification / normal-operation (`button`).

Accessories the Cambridge does **not** have are present but **commented out**:
pump 2, blower, mister, aux, circulation pump.

### How to change available accessories

Edit `packages/spa_full_superset.yaml`:

- **Add an accessory** your detection log reports but that is commented out —
  uncomment its block. Example: a second pump:

  ```yaml
  fan:
    - platform: balboa_spa   # existing pump 1
      type: pump
      index: 1
      speed_count: 2
      name: "Spa Jets"
    - platform: balboa_spa   # add pump 2
      type: pump
      index: 2
      speed_count: 2
      name: "Spa Jets 2"
  ```

- **Change a pump's speeds** — set `speed_count: 1` (on/off, exposed as a fan with
  a single speed) or `2` (low/high).
- **Add a circulation pump / blower / mister / aux** — uncomment the matching block.
- **Index is 1-based** in YAML (`index: 1` = the first pump/light); it is converted
  to 0-based internally.

After editing, re-flash:

```sh
esphome run balboa-spa.yaml
```

---

## Quick reference

| What | File | Key(s) |
|---|---|---|
| WiFi SSID / password | `secrets.yaml` | `wifi_ssid`, `wifi_password` |
| Fallback AP password | `secrets.yaml` | `ap_password` |
| MQTT host | `secrets.yaml` | `mqtt_broker` |
| MQTT login | `secrets.yaml` | `mqtt_username`, `mqtt_password` |
| MQTT topic prefix / HA discovery | `balboa-spa.yaml` | `topic_prefix`, `discovery` |
| OTA password | `secrets.yaml` | `ota_password` |
| Which pumps / lights / accessories | `packages/spa_full_superset.yaml` | per-entity `type` / `index` / `speed_count` |
| Read-only safety (no transmit) | `balboa-spa.yaml` | `read_only` (default `true`) |
| UART pins / baud | `balboa-spa.yaml` | `uart:` (`tx_pin` GPIO17, `rx_pin` GPIO16, `115200`) |

> Safety: the firmware ships with `read_only: true` — it decodes and publishes spa
> state but never transmits onto the RS-485 bus. Only set `read_only: false` after
> confirming clean decode against your spa, per [`docs/bring-up.md`](docs/bring-up.md).
