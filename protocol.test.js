'use strict';
// Offline protocol verification — no hardware needed. Run: node --test
// Fixtures are the authoritative frames verified (Python+Ruby) during the firmware build.
const { test } = require('node:test');
const assert = require('node:assert');
const P = require('./protocol');

const hex = (s) => Buffer.from(s.replace(/\s+/g, ''), 'hex');
const scan1 = (s) => P.scanFrame(hex(s)).frame;

test('crc8 matches the canonical config-request body', () => {
  assert.strictEqual(P.crc8(hex('05 0a bf 04')), 0x77);
  assert.strictEqual(P.crc8(Buffer.alloc(0)), 0x00);
  assert.strictEqual(P.crc8(Buffer.from([0x00])), 0x0c);
});

test('scanFrame parses a Ready and skips leading garbage + bad CRC', () => {
  assert.strictEqual(P.decode(scan1('7e 05 10 bf 06 5c 7e')).kind, 'ready');
  const g = P.scanFrame(hex('11 22 7e 05 10 bf 06 5c 7e'));
  assert.strictEqual(P.decode(g.frame).kind, 'ready');
  assert.strictEqual(g.consumed, 9);
  // bad CRC (5d) then a good Ready — must resync to the good one
  const b = P.scanFrame(hex('7e 05 10 bf 06 5d 7e 7e 05 10 bf 06 5c 7e'));
  assert.strictEqual(P.decode(b.frame).kind, 'ready');
});

test('decode Status', () => {
  const s = P.decode(scan1(
    '7e 1d ff af 13 00 00 64 0e 1e 00 00 00 00 00 34 01 00 02 03 00 00 00 00 00 66 00 00 00 0f 7e'));
  assert.strictEqual(s.kind, 'status');
  assert.strictEqual(s.currentTemp, 100);
  assert.strictEqual(s.targetTemp, 102);
  assert.strictEqual(s.hour, 14);
  assert.strictEqual(s.minute, 30);
  assert.strictEqual(s.heating, true);
  assert.strictEqual(s.tempRange, 'high');
  assert.strictEqual(s.tempScale, 'F');
  assert.strictEqual(s.pumps[0], 1);
  assert.strictEqual(s.circulationPump, true);
  assert.strictEqual(s.lights[0], true);
  assert.strictEqual(s.hold, false);
});

test('decode ControlConfiguration (model/version)', () => {
  const i = P.decode(scan1(
    '7e 1a 0a bf 24 64 dc 11 00 42 46 42 50 32 30 20 20 01 3d 12 38 2e 01 0a 04 00 9b 7e'));
  assert.strictEqual(i.kind, 'info');
  assert.strictEqual(i.model, 'BFBP20');
  assert.strictEqual(i.version, 'V17.0');
});

test('decode ControlConfiguration2 (accessory inventory)', () => {
  const c = P.decode(scan1('7e 0b 0a bf 2e 0a 00 01 d0 00 44 6f 7e'));
  assert.strictEqual(c.kind, 'config');
  assert.strictEqual(c.pumps[0], 2);
  assert.strictEqual(c.pumps[1], 2);
  assert.strictEqual(c.pumps[2], 0);
  assert.strictEqual(c.lights[0], true);
  assert.strictEqual(c.circulationPump, true);
});

test('decode FilterCycles', () => {
  const f = P.decode(scan1('7e 0d 0a bf 23 08 00 02 00 94 00 01 1e d0 7e'));
  assert.strictEqual(f.cycle1.startHour, 8);
  assert.strictEqual(f.cycle1.durationMin, 120);
  assert.strictEqual(f.cycle2.enabled, true);
  assert.strictEqual(f.cycle2.startHour, 20);
  assert.strictEqual(f.cycle2.durationMin, 90);
});

test('encoders produce the authoritative TX frames', () => {
  assert.strictEqual(P.encodeToggleItem(P.ITEM.light1).toString('hex'), '7e070abf11110093 7e'.replace(/\s/g, ''));
  assert.strictEqual(P.encodeSetTargetTemp(100).toString('hex'), '7e060abf2064297e');
  assert.strictEqual(P.encodeConfigRequest().toString('hex'), '7e050abf04777e');
  assert.strictEqual(P.encodeControlConfigRequest(2).toString('hex'), '7e080abf220000015 87e'.replace(/\s/g, ''));
});

test('build/scan round-trip', () => {
  const f = P.buildFrame(0x0a, 0xbf, 0x20, Buffer.from([0x64]));
  const r = P.scanFrame(f);
  assert.strictEqual(r.consumed, f.length);
  assert.strictEqual(r.frame.t1, 0x20);
  assert.strictEqual(r.frame.payload[0], 0x64);
});
