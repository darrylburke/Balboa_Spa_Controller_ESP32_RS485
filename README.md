# spa-serial-tester

A small Node.js bench tool to **read / monitor / confirm the Balboa RS-485 protocol** over a local USB adapter — to validate the protocol before committing to the ESP32 gateway.

Hardware: **Waveshare Industrial USB-to-RS485** (FT232RL + SP485EEN, auto-direction) on `/dev/ttyUSB0`, 115200 8N1. Auto-direction means both **reading and sending** work directly — no RTS/DE toggling needed.

## Setup
```sh
# Node 22 recommended (repo has .nvmrc)
source ~/.nvm/nvm.sh && nvm use 22   # or use your Node >= 20
npm install                          # installs serialport
```
You must be able to read/write the port. Check: `ls -l /dev/ttyUSB0` and `id -nG | grep dialout`. If not in `dialout`: `sudo usermod -aG dialout $USER` then re-login (or run with `sudo`).

## Prove the decoder offline (no spa needed)
```sh
npm test          # runs protocol.test.js against authoritative Balboa frames
```
All tests passing means the CRC, framing, decoders, and encoders are correct before you ever plug into the spa.

## Wire it up
Connect the adapter's **A / B** screw terminals to the spa's RS-485 **± pair** (via a Y-cable tap on the Balboa connector, or the Wi-Fi-module port). If only garbage decodes, **swap A and B** — it's non-destructive.

> Do **not** connect A/B to the +12 V pins on the Balboa connector. Verify with a multimeter (the RS-485 pair reads ~2–3 V; the 12 V pair reads 12–14 V — leave that one alone).

## Run the guided tester
```sh
node spa-tester.js                 # defaults to /dev/ttyUSB0 @ 115200
node spa-tester.js --port /dev/ttyUSB1 --baud 115200
```
It starts **read-only** and prints a live status line. Commands (single keypress):

| key | action |
|---|---|
| `m` | monitor snapshot + frame/counter stats |
| `c` | **confirm model** — requests model/version + accessory inventory + filter cycles |
| `1` | toggle Light (asks `y` to confirm — moves real equipment) |
| `2` | toggle Pump 1 (confirm) |
| `+` / `-` | nudge target temperature ±1° (confirm) |
| `h` | help · `q` / Ctrl-C | quit |

### The 3-step walkthrough
1. **Read** — watch the live line update ~1/sec (`102°F → 104° | HEAT | ready | p1:hi | light1 | 14:30`). If it stays `waiting for data…`, check A/B wiring (and swap ±). Press `m` for counters.
2. **Confirm model** — press `c`. It sends config requests on the next `Ready` window and prints your spa's **model/firmware** and **detected accessories**.
3. **Perform an action** — press `1` (light) or `+` (temp), confirm with `y`. The tool sends on the next `Ready`, then watches the next status broadcast and prints `✓ confirmed` (or `✗ no echo` with a hint).

## Notes
- **Send discipline:** the tool transmits only immediately after a `Ready` (`10 bf 06`) — the shared half-duplex bus rule — as client address `0x0A`. For reliable sends, no other client (an ESP32/panel Wi-Fi module) should also be claiming `0x0A` on the bus. Passive monitoring needs no such care.
- Protocol logic lives in `protocol.js` (pure, unit-tested in `protocol.test.js`); the CLI is `spa-tester.js`.
