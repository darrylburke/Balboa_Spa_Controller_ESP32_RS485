# Bring-up runbook

## Stage 0 — host unit tests
```
make test    # all protocol/codec/engine tests must pass
```

## Toolchain
This config needs a recent ESPHome (it uses `climate_schema()` / `new_climate()`).
An older ESPHome fails at config time with
`module 'esphome.components.climate' has no attribute 'climate_schema'` — that is
the wrong interpreter, not a code bug. Check `esphome version` before debugging.

If `esphome compile` dies with `Error: Missing Arduino framework directory 'None'`
*before* compiling anything, PlatformIO has a package-slot mismatch: the installed
`framework-arduinoespressif32` came from a different URL spec than this platform
requires, and PIO can neither reuse it nor install over it. Compare `spec:` in
`~/.platformio/packages/framework-arduinoespressif32/.piopm` against
`platform_packages` in `.esphome/build/<name>/platformio.ini`; if they differ, move
the package aside and recompile so PIO refetches the right one:
```
mv ~/.platformio/packages/framework-arduinoespressif32 \
   ~/.platformio/packages/framework-arduinoespressif32.bak
```
Note the protocol sources build to `src/protocol/*.cpp.o`, *not* under
`src/esphome/components/balboa_spa/` — grepping there for your changes gives a
false negative. Confirm a build really contains your code with
`strings firmware.elf` or `nm -C firmware.elf`.

## Stage 1 — simulator (no real spa)
> **Note:** the reference gem's `bwa_server` is **not** an RS-485 simulator. It is
> `TCPServer.open(4257)` and emulates the Balboa *WiFi module's TCP* interface;
> it takes no serial-port argument and cannot drive a USB-RS485 dongle. An
> earlier version of this doc claimed otherwise — that step never worked.

To exercise this firmware without a spa you need something that emulates the
**controller side of the RS-485 bus**: Ready polling round-robin, the new-client
channel handshake (`FE BF 00/01/02/03`), and periodic Status broadcasts. Nothing
off-the-shelf does this; `../spa-serial-tester/protocol.js` has the verified
codec to build one on.

Once such a simulator exists, wire the RS-485 A+/B- to its dongle, flash
`balboa-spa.yaml` with `read_only: false`, and confirm in the ESPHome logs:
- a "Detected spa" line with model + accessory inventory,
- decoded status (temperature, pump, light),
- command round-trips when you toggle entities.

## Stage 2 — real spa, READ ONLY
Keep `read_only: true`. Wire to the spa. Confirm decoded values match the topside
panel (temperature, heating, pump speed, light). Watch for CRC/parse errors.
If you see only garbage: swap A+/B-.

> In `read_only: true` the firmware is **fully passive** — it does not run the join
> handshake and never takes a channel. So you will *not* see a channel assignment
> in this stage; that is expected, not a fault.

Distinguish the two failure modes: **garbage** means signal is arriving with the
wrong polarity or baud; **total silence** (zero bytes) means no signal is reaching
the receiver at all, which is wiring, not protocol. For silence, measure at the
adapter's screw terminals themselves rather than at the connector — a wire clamped
on insulation reads fine upstream and nothing at the terminal.

## Stage 3 — real spa, CONTROL
Set `read_only: false`, re-flash (OTA). On first join the log should show:
```
[balboa_spa] Controller assigned bus channel 0x11 (persisting)
```
Reboot and confirm it reuses that channel rather than taking a new one:
```
[balboa_spa] Resuming saved bus channel 0x11
```
That second line is the point of the persistence: the controller never reclaims a
channel, so re-negotiating on every boot leaks one each time.

If the spa is power-cycled it forgets all clients. The firmware notices it is no
longer being polled and rejoins automatically:
```
[balboa_spa] Bus channel went stale (no windows offered); rejoining
```

Then verify each control: target temp, jets (low/high), light, heating mode,
filter cycles.

### Bus hygiene
- Channels are assigned up to `0x2f` and are **never reclaimed** — there is no
  deregister message anywhere in the protocol. Leaked channels keep being polled
  forever and measurably degrade the bus (observed with five dead channels:
  Ready→response latency 1.19 ms → 14.17 ms, status rate down ~18%).
- **A spa power cycle is the only known way to clear them.** Worth doing before a
  first flash so the ESP32 starts on a clean bus.
- Coexistence is fine: another client (topside panel / WiFi module) normally holds
  `0x10`, and the controller alternates polling between it and us.

## Bench tool
`../spa-serial-tester/` drives the same protocol from a laptop over a USB-RS485
adapter — useful for confirming the bus and the spa's behaviour independently of
the ESP32. It implements the same channel negotiation and can resume a previously
assigned channel with `--channel <hex>`.

## Verified against the real spa (2026-07-26)
- Pack **CNBP501X**, firmware **V36.0**; accessories: pump1 (2-speed) + light1.
- Filter 1 20:00 for 2:00; filter 2 enabled 08:00 for 2:00.
- `Manuals/MXBP501...pdf` is the matching platform manual. On the pack, **J33 and
  J45 are parallel 4-pin MAIN PANEL connectors** — either can host a client, so
  using a spare one avoids splicing a Y-cable into the panel run. On the 4-pin
  micro-fit the pairs sit diagonally: one diagonal pair reads 12–14 V (do **not**
  connect it), the other reads ~2–3 V and is the RS-485 pair.
- Confirmed end-to-end from the bench tool: channel negotiation, model / accessory
  / filter-cycle reads, light toggle, pump toggle, target temperature, and
  heating-mode toggle — each verified against the spa's own status echo.

## Expected entity set (Cambridge)
water heater, 1× 2-speed pump (fan), 1× light, heating-mode & temp-range & temp-scale
selects, current/target temp sensors, heating/priming/filter binary sensors,
model/version/notification text sensors, filter-cycle numbers, clear-notification &
normal-operation buttons.
