# Bring-up runbook

## Stage 0 — host unit tests
```
make test    # all protocol/codec/engine tests must pass
```

## Stage 1 — simulator (no real spa)
The reference gem ships a spa simulator. Run it on a USB-RS485 dongle:
```
cd ../balboa_worldwide_app && bundle exec exe/bwa_server /dev/ttyUSB0
```
Wire the HW-0519 A+/B- to the dongle's A/B, flash `balboa-spa.yaml` with
`read_only: false` for this bench test, and confirm in the ESPHome logs:
- a "Detected spa" line with model + accessory inventory,
- decoded status (temperature, pump, light),
- command round-trips when you toggle entities.

## Stage 2 — real spa, READ ONLY
Keep `read_only: true`. Wire to the spa. Confirm decoded values match the topside
panel (temperature, heating, pump speed, light). Watch for CRC/parse errors in logs.
If you see only garbage: swap A+/B-.

## Stage 3 — real spa, CONTROL
Set `read_only: false`, re-flash (OTA). Verify each control: target temp, jets
(low/high), light, heating mode, filter cycles. The ESP32 must be the only add-on
client on the bus (Balboa WiFi module removed).

## Expected entity set (Cambridge)
water heater, 1× 2-speed pump (fan), 1× light, heating-mode & temp-range & temp-scale
selects, current/target temp sensors, heating/priming/filter binary sensors,
model/version/notification text sensors, filter-cycle numbers, clear-notification &
normal-operation buttons.
