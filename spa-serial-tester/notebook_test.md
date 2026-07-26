# Balboa RS-485 Bench Test — Instructions

A small Node.js tool that reads/monitors/confirms the Balboa protocol over a **Waveshare USB-RS485** adapter (FT232RL + SP485EEN, auto-direction — so **sending works too**, not just reading). Use it to validate the protocol on the bench before committing to the ESP32 gateway.

Location: `/home/darrylb/projects/SPA/spa-serial-tester/`

## What's in it
- **`protocol.js`** — pure JS port of the Balboa protocol (CRC-8, `~`-framing, decoders, encoders); the same protocol reverse-engineered for the firmware.
- **`protocol.test.js`** — 8 offline tests asserting the decoder/encoder against authoritative frames (Status → 100/102 °F, model → `BFBP20`/V17.0, toggle-light bytes, etc.). Proves the decode is correct **before you plug into the spa**.
- **`spa-tester.js`** — the interactive guided CLI (`serialport`).

## Setup
```sh
cd /home/darrylb/projects/SPA/spa-serial-tester
source ~/.nvm/nvm.sh && nvm use 22    # Node 22 (repo has .nvmrc)
npm install                           # installs serialport (first time only)
```
You must be able to read/write the port. Check:
```sh
ls -l /dev/ttyUSB0        # should exist, group "dialout"
id -nG | grep dialout     # you should be in the dialout group
```
If not in `dialout`: `sudo usermod -aG dialout $USER`, then re-login (or run the tool with `sudo`).

## Step 0 — Prove the decoder offline (no spa needed)
```sh
npm test
```
Expect **8 passing tests**. This confirms the CRC, framing, decoders, and encoders are correct before you ever touch the spa.

## Step 1 — Wire it up
Connect the adapter's **A / B** screw terminals to the spa's RS-485 **± pair** — via a Y-cable tap on the Balboa connector, or the Wi-Fi-module port.
- If only garbage decodes, **swap A and B** (non-destructive).
- ⚠️ Do **not** connect A/B to the **+12 V** pins on the Balboa connector. With a multimeter: the RS-485 pair reads ~2–3 V; the 12 V pair reads 12–14 V — leave that pair alone.

## Step 2 — Run the guided tester
```sh
node spa-tester.js                     # defaults to /dev/ttyUSB0 @ 115200
# or:
node spa-tester.js --port /dev/ttyUSB1 --baud 115200
```
It starts **read-only** and prints a live status line. Single-keypress commands:

| key | action |
|---|---|
| `m` | monitor snapshot + frame/counter stats |
| `c` | **confirm model** — requests model/version + accessory inventory + filter cycles |
| `1` | toggle Light (asks `y` to confirm — moves real equipment) |
| `2` | toggle Pump 1 (confirm) |
| `+` / `-` | nudge target temperature ±1° (confirm) |
| `h` | help |
| `q` / `Ctrl-C` | quit |

## The 3-step walkthrough
1. **Read** — watch the live line update ~1/sec, e.g.:
   ```
   102°F → 104°  | HEAT | ready | p1:hi | light1 | 14:30
   ```
   If it stays `waiting for data…`, check the A/B wiring and try swapping RS-485 ±. Press `m` for counters (if `status=0` stays, it's a wiring issue).
2. **Confirm model** — press **`c`**. It sends config requests on the next `Ready` window and prints your spa's **model / firmware** and **detected accessories** (pumps & speeds, lights, blower, circ pump, filter cycles).
3. **Perform an action** — press **`1`** (light), **`2`** (pump), or **`+`/`-`** (temp); confirm with **`y`**. The tool sends on the next `Ready`, then watches the next status broadcast and prints:
   - `✓ confirmed: <action>` — the spa echoed the change, or
   - `✗ no confirming status echo …` — with a hint (check we're the only `0x0A` client / wiring).

## Important notes
- **Send discipline:** the tool transmits only immediately after a `Ready` (`10 bf 06`) message — the shared half-duplex bus rule — acting as bus client address **`0x0A`**.
- For **reliable sends** (steps `c`, `1`, `2`, `+`, `-`), make sure **no other client is also claiming `0x0A`** on the bus (e.g., a Balboa Wi-Fi module, or the ESP32 running the gateway firmware). Pure **monitoring** (the live line) needs no such care — it just listens.
- Terminal rendering: the live status updates in place on one line. If you redirect output to a file you'll see raw `\x1b[K` clear-line codes — that's cosmetic, only relevant when piped.

## Next step
Once the protocol is confirmed on the bench, the ESP32 firmware in `../balboa-esp32-mqtt/` is ready to take over as the permanent MQTT gateway.
