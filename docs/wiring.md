# Wiring

ESP32 dev board + HW-0519 auto-direction RS-485 module.

| ESP32 | HW-0519 |
|-------|---------|
| GPIO17 (TX) | RXD |
| GPIO16 (RX) | TXD |
| 3V3 | VCC |
| GND | GND |

| HW-0519 | Spa |
|---------|-----|
| A+ | RS-485 + |
| B- | RS-485 - |

The HW-0519 handles RS-485 direction automatically — no DE/RE GPIO.

## Finding the spa's RS-485 wires
On the Balboa pack's micro-fit connector, one opposite pin pair reads **12–14 V**
(do NOT connect this). With one meter probe on that pair's negative pin, the other
two wires read ~2–3 V: the slightly higher one is **RS-485+**, the slightly lower is
**RS-485-**. Swapping +/- only produces garbage (non-destructive) — swap back to fix.

This spa: **Canadian Spa Co. Cambridge (Black Ice), Balboa pack** — replace/stand in
for the WiFi module position.

**Bus addressing:** the ESP32 does *not* use a fixed address. It starts unregistered
and negotiates a channel with the controller (`FE BF 00` → `FE BF 01` → `FE BF 02`
→ ack), then transmits only in a Ready addressed to that channel. This spa already
has another client holding `0x10`, so a hardcoded address collides with it. In
`read_only: true` mode the ESP32 never joins the bus at all and stays fully passive.
