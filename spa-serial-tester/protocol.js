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
  NEW_CLIENT: [0xbf, 0x00],   // controller: "any new clients?" (on channel 0xfe)
  ID_REQUEST: [0xbf, 0x01],   // client -> controller: request a channel
  ID_ASSIGN: [0xbf, 0x02],    // controller -> client: here is your channel
  ID_ACK: [0xbf, 0x03],       // client -> controller: acknowledge assignment
  NOTHING_TO_SEND: [0xbf, 0x07],
  TOGGLE: [0xbf, 0x11],
  CTRL_CFG: [0xbf, 0x24],   // info: model + version
  CTRL_CFG2: [0xbf, 0x2e],  // accessory inventory
  FILTER: [0xbf, 0x23],
};
const is = (f, t) => f.t0 === t[0] && f.t1 === t[1];
const isReady = (f) => is(f, TYPE.READY);
const isNewClient = (f) => is(f, TYPE.NEW_CLIENT);

// Channel negotiation. The bus is shared: a client MUST be assigned a channel and
// may only transmit in a Ready addressed to that channel. Never assume an address.
const BROADCAST = 0xfe;          // channel used for the join handshake
const MAX_CHANNEL = 0x2f;        // controller assignments are capped here
const isNewClientCTS = (f) => f.src === BROADCAST && is(f, TYPE.NEW_CLIENT);
const isChannelAssignment = (f) => f.src === BROADCAST && is(f, TYPE.ID_ASSIGN);
// Ready addressed specifically to us — the only window we may transmit in.
const isReadyFor = (f, id) => is(f, TYPE.READY) && f.src === id;
// Channel from an ID_ASSIGN frame, clamped to the controller's valid range.
const channelFromAssignment = (f) => Math.min(f.payload[0], MAX_CHANNEL);

// Heating mode is NOT a dense 0..2 range: Ready-in-Rest is 3, not 2 (per the
// Balboa protocol doc and cribskip). Indexing an array here silently yields
// undefined whenever the spa is in Ready-in-Rest.
const HEATING_MODE = { 0x00: 'ready', 0x01: 'rest', 0x03: 'ready_in_rest' };
const NOTIFICATION = { 0x00: null, 0x0a: 'ph', 0x04: 'filter', 0x09: 'sanitizer' };

function decode(f) {
  if (is(f, TYPE.STATUS)) return { kind: 'status', ...decodeStatus(f.payload) };
  if (is(f, TYPE.CTRL_CFG)) return { kind: 'info', ...decodeControlConfig(f.payload) };
  if (is(f, TYPE.CTRL_CFG2)) return { kind: 'config', ...decodeControlConfig2(f.payload) };
  if (is(f, TYPE.FILTER)) return { kind: 'filter', ...decodeFilterCycles(f.payload) };
  if (isReady(f)) return { kind: 'ready' };
  if (isChannelAssignment(f)) return { kind: 'id_assign', channel: channelFromAssignment(f) };
  if (isNewClient(f)) return { kind: 'new_client' };
  if (is(f, TYPE.NOTHING_TO_SEND)) return { kind: 'nothing_to_send' };
  if (is(f, TYPE.TOGGLE)) return { kind: 'toggle', item: f.payload[0] };
  return { kind: 'unknown', t0: f.t0, t1: f.t1 };
}

function decodeStatus(d) {
  const celsius = (d[9] & 0x01) === 0x01;
  const rawCur = d[2];
  const scaleTemp = (v) => (celsius ? v / 2 : v);
  return {
    hold: (d[0] & 0x05) !== 0,
    priming: d[1] === 0x01,
    heatingMode: HEATING_MODE[d[5] & 0x03] ?? 'unknown',
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

// ---- encoders ----
// `src` is our assigned channel. It defaults to 0x0a only for offline/unit-test use;
// on a real bus, always pass the channel the controller assigned via the handshake.
const ITEM = {
  normal_operation: 0x01, clear_notification: 0x03,
  pump1: 0x04, pump2: 0x05, pump3: 0x06, blower: 0x0c, mister: 0x0e,
  light1: 0x11, light2: 0x12, aux1: 0x16, aux2: 0x17,
  soak: 0x1d, hold: 0x3c, temperature_range: 0x50, heating_mode: 0x51,
};

const encodeToggleItem = (item, src = 0x0a) => buildFrame(src, 0xbf, 0x11, Buffer.from([item, 0x00]));
const encodeSetTargetTemp = (raw, src = 0x0a) => buildFrame(src, 0xbf, 0x20, Buffer.from([raw & 0xff]));
const encodeConfigRequest = (src = 0x0a) => buildFrame(src, 0xbf, 0x04, Buffer.alloc(0));
function encodeControlConfigRequest(type, src = 0x0a) {
  const p = { 1: [0x02, 0x00, 0x00], 2: [0x00, 0x00, 0x01], 3: [0x01, 0x00, 0x00] }[type] || [0, 0, 0];
  return buildFrame(src, 0xbf, 0x22, Buffer.from(p));
}

// Write filter cycles back to the spa. Same message type as the response:
// [c1 hour][c1 min][c1 dur h][c1 dur m][c2 hour|0x80 if enabled][c2 min][c2 dur h][c2 dur m]
// NOTE: unlike the toggles, this changes a PERSISTENT spa setting.
function encodeFilterCycles(fc, src = 0x0a) {
  const p = Buffer.alloc(8);
  p[0] = fc.cycle1.startHour & 0xff;
  p[1] = fc.cycle1.startMinute & 0xff;
  p[2] = Math.floor(fc.cycle1.durationMin / 60) & 0xff;
  p[3] = (fc.cycle1.durationMin % 60) & 0xff;
  p[4] = (fc.cycle2.startHour & 0x7f) | (fc.cycle2.enabled ? 0x80 : 0x00);
  p[5] = fc.cycle2.startMinute & 0xff;
  p[6] = Math.floor(fc.cycle2.durationMin / 60) & 0xff;
  p[7] = (fc.cycle2.durationMin % 60) & 0xff;
  return buildFrame(src, TYPE.FILTER[0], TYPE.FILTER[1], p);
}

// ---- channel negotiation encoders ----
// 0x02 0xf1 0x73 is the client device signature used by the reference implementations.
const CLIENT_SIGNATURE = Buffer.from([0x02, 0xf1, 0x73]);
const encodeIdRequest = () => buildFrame(BROADCAST, 0xbf, 0x01, CLIENT_SIGNATURE);
const encodeIdAck = (id) => buildFrame(id, 0xbf, 0x03);
const encodeNothingToSend = (id) => buildFrame(id, 0xbf, 0x07);

module.exports = {
  crc8, scanFrame, buildFrame, decode, isReady, isNewClient, TYPE, ITEM,
  encodeToggleItem, encodeSetTargetTemp, encodeConfigRequest, encodeControlConfigRequest,
  encodeFilterCycles,
  BROADCAST, MAX_CHANNEL, CLIENT_SIGNATURE,
  isNewClientCTS, isChannelAssignment, isReadyFor, channelFromAssignment,
  encodeIdRequest, encodeIdAck, encodeNothingToSend,
};
