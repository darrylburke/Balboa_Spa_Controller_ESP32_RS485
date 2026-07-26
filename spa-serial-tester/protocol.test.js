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

test('Ready is only ours when addressed to our channel', () => {
  const ready10 = scan1('7e 05 10 bf 06 5c 7e');   // addressed to channel 0x10
  assert.strictEqual(P.isReady(ready10), true);
  assert.strictEqual(P.isReadyFor(ready10, 0x10), true);
  // the bug this guards: a client on another channel must NOT treat this as its window
  assert.strictEqual(P.isReadyFor(ready10, 0x11), false);
  assert.strictEqual(P.isReadyFor(ready10, 0x0a), false);
  assert.strictEqual(P.isReadyFor(ready10, 0x00), false);
});

test('new-client CTS is recognised only on the 0xfe broadcast channel', () => {
  const cts = P.scanFrame(P.buildFrame(0xfe, 0xbf, 0x00)).frame;
  assert.strictEqual(P.isNewClientCTS(cts), true);
  assert.strictEqual(P.decode(cts).kind, 'new_client');
  // same message type but on a normal channel is not a join invitation
  const notCts = P.scanFrame(P.buildFrame(0x10, 0xbf, 0x00)).frame;
  assert.strictEqual(P.isNewClientCTS(notCts), false);
});

test('channel assignment decodes and clamps to MAX_CHANNEL', () => {
  const a = P.scanFrame(P.buildFrame(0xfe, 0xbf, 0x02, Buffer.from([0x11]))).frame;
  assert.strictEqual(P.isChannelAssignment(a), true);
  assert.strictEqual(P.channelFromAssignment(a), 0x11);
  assert.strictEqual(P.decode(a).kind, 'id_assign');
  assert.strictEqual(P.decode(a).channel, 0x11);
  // out-of-range assignment is clamped, matching the reference implementation
  const big = P.scanFrame(P.buildFrame(0xfe, 0xbf, 0x02, Buffer.from([0x9c]))).frame;
  assert.strictEqual(P.channelFromAssignment(big), P.MAX_CHANNEL);
});

test('handshake encoders round-trip', () => {
  const req = P.scanFrame(P.encodeIdRequest()).frame;
  assert.strictEqual(req.src, P.BROADCAST);
  assert.strictEqual(req.t1, 0x01);
  assert.deepStrictEqual(Buffer.from(req.payload), P.CLIENT_SIGNATURE);

  const ack = P.scanFrame(P.encodeIdAck(0x11)).frame;
  assert.strictEqual(ack.src, 0x11);
  assert.strictEqual(ack.t1, 0x03);

  const nts = P.scanFrame(P.encodeNothingToSend(0x11)).frame;
  assert.strictEqual(nts.src, 0x11);
  assert.strictEqual(P.decode(nts).kind, 'nothing_to_send');
});

test('encoders transmit on the assigned channel, not a hardcoded one', () => {
  const f = P.scanFrame(P.encodeControlConfigRequest(1, 0x11)).frame;
  assert.strictEqual(f.src, 0x11);
  assert.strictEqual(f.t1, 0x22);
  assert.strictEqual(P.scanFrame(P.encodeToggleItem(P.ITEM.light1, 0x11)).frame.src, 0x11);
  assert.strictEqual(P.scanFrame(P.encodeSetTargetTemp(100, 0x11)).frame.src, 0x11);
});

test('decode classifies the idle toggle frame seen on a live bus', () => {
  const t = P.scanFrame(P.buildFrame(0x10, 0xbf, 0x11, Buffer.from([0x00, 0x00]))).frame;
  const m = P.decode(t);
  assert.strictEqual(m.kind, 'toggle');
  assert.strictEqual(m.item, 0x00);
});

test('heating mode decodes the sparse encoding (ready_in_rest is 3, not 2)', () => {
  // Flags-2 low bits carry the mode; build a status frame per value.
  const withMode = (v) => {
    const p = Buffer.alloc(24);
    p[5] = v;
    p[2] = 100; p[20] = 102;
    return P.decode(P.scanFrame(P.buildFrame(0xff, 0xaf, 0x13, p)).frame).heatingMode;
  };
  assert.strictEqual(withMode(0), 'ready');
  assert.strictEqual(withMode(1), 'rest');
  assert.strictEqual(withMode(3), 'ready_in_rest');
  assert.strictEqual(withMode(2), 'unknown');   // unused value must not crash
});

test('heating mode toggle uses item 0x51 on our channel', () => {
  const f = P.scanFrame(P.encodeToggleItem(P.ITEM.heating_mode, 0x11)).frame;
  assert.strictEqual(f.src, 0x11);
  assert.strictEqual(f.t0, 0xbf);
  assert.strictEqual(f.t1, 0x11);
  assert.strictEqual(f.payload[0], 0x51);
});

test('encodeFilterCycles round-trips through the decoder', () => {
  const fc = {
    cycle1: { startHour: 20, startMinute: 0, durationMin: 120 },
    cycle2: { enabled: true, startHour: 8, startMinute: 30, durationMin: 90 },
  };
  const back = P.decode(P.scanFrame(P.encodeFilterCycles(fc, 0x11)).frame);
  assert.strictEqual(back.kind, 'filter');
  assert.deepStrictEqual(back.cycle1, fc.cycle1);
  assert.strictEqual(back.cycle2.enabled, true);
  assert.strictEqual(back.cycle2.startHour, 8);
  assert.strictEqual(back.cycle2.startMinute, 30);
  assert.strictEqual(back.cycle2.durationMin, 90);
});

test('encodeFilterCycles reproduces the payload this spa actually sent', () => {
  // Captured live: 14 00 02 00 88 00 02 00
  const fc = {
    cycle1: { startHour: 0x14, startMinute: 0, durationMin: 120 },
    cycle2: { enabled: true, startHour: 8, startMinute: 0, durationMin: 120 },
  };
  const f = P.scanFrame(P.encodeFilterCycles(fc, 0x13)).frame;
  assert.strictEqual(Buffer.from(f.payload).toString('hex'), '1400020088000200');
  assert.strictEqual(f.src, 0x13);
});

test('disabling cycle2 clears the enable bit but keeps the hour', () => {
  const fc = {
    cycle1: { startHour: 6, startMinute: 15, durationMin: 60 },
    cycle2: { enabled: false, startHour: 8, startMinute: 0, durationMin: 120 },
  };
  const f = P.scanFrame(P.encodeFilterCycles(fc, 0x11)).frame;
  assert.strictEqual(f.payload[4] & 0x80, 0);
  assert.strictEqual(f.payload[4] & 0x7f, 8);
  assert.strictEqual(P.decode(f).cycle2.enabled, false);
});

test('build/scan round-trip', () => {
  const f = P.buildFrame(0x0a, 0xbf, 0x20, Buffer.from([0x64]));
  const r = P.scanFrame(f);
  assert.strictEqual(r.consumed, f.length);
  assert.strictEqual(r.frame.t1, 0x20);
  assert.strictEqual(r.frame.payload[0], 0x64);
});
