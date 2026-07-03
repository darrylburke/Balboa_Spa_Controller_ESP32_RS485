'use strict';
// Balboa RS-485 protocol — pure JS port (no hardware deps).
// Frame: 0x7E LEN SRC T0 T1 payload... CRC 0x7E ; LEN = payloadLen + 5.
// CRC-8: poly 0x07, init 0x02, final XOR 0x02, no reflection; over bytes [LEN .. last payload byte].

const DELIM = 0x7e;

function crc8(bytes) {
  let crc = 0x02;
  for (const b of bytes) {
    crc ^= b;
    for (let i = 0; i < 8; i++) {
      crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) & 0xff : (crc << 1) & 0xff;
    }
  }
  return crc ^ 0x02;
}

// Scan `buf` (Buffer) for the next valid frame.
// Returns { frame, consumed, skipped } on success (frame = { src, t0, t1, payload:Buffer }),
// or { consumed, skipped } with no `frame` when more data is needed.
// `consumed` = bytes the caller should drop; `skipped` = leading garbage/discarded bytes (noise).
function scanFrame(buf) {
  let offset = 0;
  while (true) {
    if (offset + 5 > buf.length) {
      // keep from the last possible frame start
      let keep = offset;
      while (keep < buf.length && buf[keep] !== DELIM) keep++;
      return { consumed: keep, skipped: keep };
    }
    if (buf[offset] !== DELIM) { offset++; continue; }
    const len = buf[offset + 1];
    if (len < 5 || len >= DELIM) { offset++; continue; }
    if (offset + len + 2 > buf.length) return { consumed: offset, skipped: offset }; // partial; wait for more
    if (buf[offset + len + 1] !== DELIM) { offset++; continue; }
    if (crc8(buf.subarray(offset + 1, offset + len)) !== buf[offset + len]) { offset++; continue; }
    const frame = {
      src: buf[offset + 2],
      t0: buf[offset + 3],
      t1: buf[offset + 4],
      payload: buf.subarray(offset + 5, offset + len), // len-5 bytes
    };
    return { frame, consumed: offset + len + 2, skipped: offset };
  }
}

function buildFrame(src, t0, t1, payload = Buffer.alloc(0)) {
  const len = payload.length + 5;
  const out = Buffer.alloc(payload.length + 7);
  out[0] = DELIM; out[1] = len; out[2] = src; out[3] = t0; out[4] = t1;
  payload.copy(out, 5);
  out[5 + payload.length] = crc8(out.subarray(1, 1 + (len - 1)));
  out[6 + payload.length] = DELIM;
  return out;
}

// ---- message type identity ----
const TYPE = {
  STATUS: [0xaf, 0x13],
  READY: [0xbf, 0x06],
  NEW_CLIENT: [0xbf, 0x00],
  CTRL_CFG: [0xbf, 0x24],   // info: model + version
  CTRL_CFG2: [0xbf, 0x2e],  // accessory inventory
  FILTER: [0xbf, 0x23],
};
const is = (f, t) => f.t0 === t[0] && f.t1 === t[1];
const isReady = (f) => is(f, TYPE.READY);
const isNewClient = (f) => is(f, TYPE.NEW_CLIENT);

const HEATING_MODE = ['ready', 'rest', 'ready_in_rest'];
const NOTIFICATION = { 0x00: null, 0x0a: 'ph', 0x04: 'filter', 0x09: 'sanitizer' };

function decode(f) {
  if (is(f, TYPE.STATUS)) return { kind: 'status', ...decodeStatus(f.payload) };
  if (is(f, TYPE.CTRL_CFG)) return { kind: 'info', ...decodeControlConfig(f.payload) };
  if (is(f, TYPE.CTRL_CFG2)) return { kind: 'config', ...decodeControlConfig2(f.payload) };
  if (is(f, TYPE.FILTER)) return { kind: 'filter', ...decodeFilterCycles(f.payload) };
  if (isReady(f)) return { kind: 'ready' };
  if (isNewClient(f)) return { kind: 'new_client' };
  return { kind: 'unknown', t0: f.t0, t1: f.t1 };
}

function decodeStatus(d) {
  const celsius = (d[9] & 0x01) === 0x01;
  const rawCur = d[2];
  const scaleTemp = (v) => (celsius ? v / 2 : v);
  return {
    hold: (d[0] & 0x05) !== 0,
    priming: d[1] === 0x01,
    heatingMode: HEATING_MODE[d[5] & 0x03],
    notification: d[1] === 0x03 ? NOTIFICATION[d[6]] ?? null : null,
    tempScale: celsius ? 'C' : 'F',
    twentyFourHour: (d[9] & 0x02) !== 0,
    filterRunning: [(d[9] & 0x04) !== 0, (d[9] & 0x08) !== 0],
    heating: (d[10] & 0x30) !== 0,
    tempRange: (d[10] & 0x04) ? 'high' : 'low',
    pumps: [d[11] & 3, (d[11] >> 2) & 3, (d[11] >> 4) & 3, (d[11] >> 6) & 3, d[12] & 3, (d[12] >> 2) & 3],
    circulationPump: (d[13] & 0x02) !== 0,
    blower: (d[13] >> 2) & 3,
    lights: [(d[14] & 3) !== 0, ((d[14] >> 2) & 3) !== 0],
    mister: (d[15] & 0x01) !== 0,
    aux: [(d[15] & 0x08) !== 0, (d[15] & 0x10) !== 0],
    hour: d[3], minute: d[4],
    currentTemp: rawCur === 0xff ? null : scaleTemp(rawCur),
    targetTemp: scaleTemp(d[20]),
  };
}

function decodeControlConfig(d) {
  let model = '';
  for (let i = 4; i <= 11 && i < d.length; i++) model += String.fromCharCode(d[i]);
  return { version: `V${d[2]}.${d[3]}`, model: model.replace(/[\s\0]+$/, '') };
}

function decodeControlConfig2(d) {
  return {
    pumps: [d[0] & 3, (d[0] >> 2) & 3, (d[0] >> 4) & 3, (d[0] >> 6) & 3, d[1] & 3, (d[1] >> 6) & 3],
    lights: [(d[2] & 3) !== 0, ((d[2] >> 6) & 3) !== 0],
    blower: d[3] & 3,
    circulationPump: ((d[3] >> 6) & 3) !== 0,
    mister: (d[4] & 0x30) !== 0,
    aux: [(d[4] & 0x01) !== 0, (d[4] & 0x02) !== 0],
  };
}

function decodeFilterCycles(d) {
  return {
    cycle1: { startHour: d[0], startMinute: d[1], durationMin: d[2] * 60 + d[3] },
    cycle2: { enabled: (d[4] & 0x80) !== 0, startHour: d[4] & 0x7f, startMinute: d[5], durationMin: d[6] * 60 + d[7] },
  };
}

// ---- encoders (we transmit as client SRC 0x0A) ----
const ITEM = {
  normal_operation: 0x01, clear_notification: 0x03,
  pump1: 0x04, pump2: 0x05, pump3: 0x06, blower: 0x0c, mister: 0x0e,
  light1: 0x11, light2: 0x12, aux1: 0x16, aux2: 0x17,
  soak: 0x1d, hold: 0x3c, temperature_range: 0x50, heating_mode: 0x51,
};

const encodeToggleItem = (item) => buildFrame(0x0a, 0xbf, 0x11, Buffer.from([item, 0x00]));
const encodeSetTargetTemp = (raw) => buildFrame(0x0a, 0xbf, 0x20, Buffer.from([raw & 0xff]));
const encodeConfigRequest = () => buildFrame(0x0a, 0xbf, 0x04, Buffer.alloc(0));
function encodeControlConfigRequest(type) {
  const p = { 1: [0x02, 0x00, 0x00], 2: [0x00, 0x00, 0x01], 3: [0x01, 0x00, 0x00] }[type] || [0, 0, 0];
  return buildFrame(0x0a, 0xbf, 0x22, Buffer.from(p));
}

module.exports = {
  crc8, scanFrame, buildFrame, decode, isReady, isNewClient, TYPE, ITEM,
  encodeToggleItem, encodeSetTargetTemp, encodeConfigRequest, encodeControlConfigRequest,
};
