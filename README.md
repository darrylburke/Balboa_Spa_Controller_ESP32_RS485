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
node spa-tester.js                          # defaults to /dev/ttyUSB0 @ 115200
node spa-tester.js --port /dev/ttyUSB1 --baud 115200
node spa-tester.js --channel 12             # resume a previously assigned channel
node spa-tester.js --yes                    # skip the y/N on commands that act
```
It starts **read-only**, negotiates a bus channel, and prints a live status line
prefixed with the channel it holds. Commands (single keypress):

| key | action |
|---|---|
| `m` | monitor snapshot + counters (incl. `ours=` / `others=` Ready-window split) |
| `c` | **confirm model** — requests model/version + accessory inventory + filter cycles |
| `1` | toggle Light (asks `y` — moves real equipment) |
| `2` | toggle Pump 1 (confirm) |
| `+` / `-` | nudge target temperature ±1° (confirm) |
| `r` | toggle heating mode READY ↔ REST (confirm) |
| `f` | set filter cycle start/duration — prompts for values, **persistent setting** |
| `h` | help · `q` / Ctrl-C | quit |

### Flags
- `--channel <hex>` — resume a channel the spa assigned earlier instead of asking
  for a new one. Each negotiation permanently consumes a channel (see Notes), so
  reuse one across runs.
- `--yes` / `-y` — skip the y/N confirmation on `1 2 r + -` **and** the `f` write.
  It only removes the keypress: every command still logs the bytes it sent and
  still verifies against the spa's next status broadcast. `f` still prompts for
  the values themselves.

### The 3-step walkthrough
1. **Read** — watch the live line update ~1/sec (`ch:0x11 | 102°F → 104° | HEAT | ready | p1:hi | light1 | 14:30`). If it stays `waiting for data…`, check A/B wiring (and swap ±). Press `m` for counters.
2. **Confirm model** — press `c`. It sends config requests in our own `Ready` window and prints your spa's **model/firmware**, **detected accessories** and **filter cycles**.
3. **Perform an action** — press `1` (light) or `+` (temp), confirm with `y`. The tool sends in our next window, then watches the next status broadcast and prints `✓ confirmed` (or `✗ no echo` with a hint).

> If the live line never shows a channel (`ch:--`), we were never assigned one and
> the tool stays silent by design — it will not transmit unaddressed.

### Diagnosing a dead bus
- **Garbage bytes** — signal present, wrong polarity or baud. Swap A/B.
- **Zero bytes** — no signal reaching the receiver at all. This is wiring, not
  protocol; swapping A/B will not help. Measure at the adapter's screw terminals
  themselves, not at the connector: a wire clamped on insulation reads correctly
  upstream and nothing at the terminal.

## Bus addressing
The bus is shared **and addressed**. A client must be given a channel by the
controller and may transmit only in a `Ready` addressed to *that* channel:

```
controller → FE BF 00           "any new clients?"
client     → FE BF 01 02 F1 73  ID request
controller → FE BF 02 <id>      assigns a channel (capped at 0x2f)
client     → <id> BF 03         ack
then: transmit only on <id> BF 06, as src <id>; send <id> BF 07 when idle
```

**Never hardcode an address.** Another client — usually the topside panel — already
holds one (typically `0x10`) and answers *every* one of its Ready windows within
~1.2 ms. A client that hardcodes an address and fires on any `Ready` transmits on
top of it and both frames are lost. Press `m` to see `others=` counting the windows
this tool correctly declined.

Coexistence is fine once addressing is negotiated: the controller alternates
polling between the existing client and this tool.

## Notes
- **Channels are never reclaimed.** There is no deregister message in the protocol
  (checked against ccutrer's gem, cribskip, brianfeucht and Dakoriki). Every run
  that negotiates a new channel leaves the old one polled forever, which degrades
  the bus — with five dead channels, Ready→response latency went 1.19 ms → 14.17 ms
  and the status rate dropped ~18%. Use `--channel` to reuse one; **power-cycle the
  spa** to clear them (the only known way).
- Heating mode is a sparse encoding: Ready-in-Rest is **3**, not 2. Indexing a
  dense 0..2 table silently mis-decodes it.
- Filter cycles are written with the same `bf 23` message used to report them, and
  are a persistent spa setting — `f` prints the previous values before writing, then
  re-reads from the spa to confirm.
- Protocol logic lives in `protocol.js` (pure, unit-tested in `protocol.test.js`); the CLI is `spa-tester.js`.
