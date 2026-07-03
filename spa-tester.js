'use strict';
// Balboa RS-485 bench tester — guided read -> confirm model -> perform action.
// Usage: node spa-tester.js [--port /dev/ttyUSB0] [--baud 115200]
const { SerialPort } = require('serialport');
const readline = require('node:readline');
const P = require('./protocol');

// ---- args ----
const args = process.argv.slice(2);
const getArg = (name, def) => {
  const i = args.indexOf(name);
  return i >= 0 && args[i + 1] ? args[i + 1] : def;
};
const PATH = getArg('--port', '/dev/ttyUSB0');
const BAUD = parseInt(getArg('--baud', '115200'), 10);

// ---- state ----
let rx = Buffer.alloc(0);
const state = { status: null, info: null, config: null, filter: null };
const counters = { frames: 0, status: 0, ready: 0, info: 0, config: 0, filter: 0, unknown: 0, noise: 0 };
const txQueue = [];          // { bytes, desc }
let pending = null;          // { desc, check(status)->bool, deadline }
let lastDataAt = 0;
let confirm = null;          // { prompt, action }

// ---- output helpers (live status line via \r, logs printed above it) ----
let lastLive = '';
function log(...a) { process.stdout.write('\r\x1b[K' + a.join(' ') + '\n'); process.stdout.write(lastLive); }
function renderLive() {
  const s = state.status;
  let line;
  if (!s) {
    const age = lastDataAt ? Math.round((Date.now() - lastDataAt) / 1000) : null;
    line = lastDataAt ? `reading… ${counters.frames} frames, ${counters.ready} ready (${age}s since last)` : 'waiting for data…';
  } else {
    const t = (v) => (v == null ? '--' : v);
    const pumps = s.pumps.map((p, i) => (p ? `p${i + 1}:${p === 2 ? 'hi' : 'lo'}` : null)).filter(Boolean).join(' ') || 'pumps:off';
    const lights = s.lights.map((l, i) => (l ? `light${i + 1}` : null)).filter(Boolean).join(' ') || 'light:off';
    line = `${t(s.currentTemp)}°${s.tempScale} → ${t(s.targetTemp)}°  | ${s.heating ? 'HEAT' : 'idle'} | ${s.heatingMode} | ${s.tempRange} | ${pumps} | ${lights} | ${String(s.hour).padStart(2, '0')}:${String(s.minute).padStart(2, '0')}`;
  }
  lastLive = '\r\x1b[K' + line;
  process.stdout.write(lastLive);
}

// ---- frame handling ----
function drainFrames() {
  for (;;) {
    const r = P.scanFrame(rx);
    counters.noise += r.skipped || 0;
    if (r.frame) {
      rx = rx.subarray(r.consumed);
      handleFrame(r.frame);
    } else {
      if (r.consumed > 0) rx = rx.subarray(r.consumed);
      break;
    }
  }
}
function handleFrame(f) {
  counters.frames++;
  const m = P.decode(f);
  switch (m.kind) {
    case 'status':
      counters.status++; state.status = m; renderLive(); checkPending(m); break;
    case 'info': counters.info++; state.info = m; break;
    case 'config': counters.config++; state.config = m; break;
    case 'filter': counters.filter++; state.filter = m; break;
    case 'ready': counters.ready++; maybeSend(); break;
    case 'new_client': break;
    default: counters.unknown++;
  }
}
function maybeSend() {
  if (txQueue.length === 0) return;
  const cmd = txQueue.shift();
  port.write(cmd.bytes, (err) => {
    if (err) log('✗ write error:', err.message);
    else log('→ sent:', cmd.desc, '(' + cmd.bytes.toString('hex') + ')');
  });
}
function checkPending(status) {
  if (!pending) return;
  if (pending.check(status)) { log('✓ confirmed:', pending.desc); pending = null; }
}

// ---- actions ----
function enqueue(bytes, desc) { txQueue.push({ bytes, desc }); log('· queued:', desc, '— sending on next Ready…'); }

function confirmModel() {
  state.info = state.config = state.filter = null;
  enqueue(P.encodeControlConfigRequest(1), 'request info (model/version)');
  enqueue(P.encodeControlConfigRequest(2), 'request config (accessories)');
  enqueue(P.encodeControlConfigRequest(3), 'request filter cycles');
  setTimeout(() => {
    log('── model / config ' + '─'.repeat(30));
    if (state.info) log(`  model: ${state.info.model}   firmware: ${state.info.version}`);
    else log('  (no info response — are we the only client at 0x0A? check wiring)');
    if (state.config) {
      const c = state.config;
      const acc = [];
      c.pumps.forEach((p, i) => p && acc.push(`pump${i + 1} (${p}-speed)`));
      c.lights.forEach((l, i) => l && acc.push(`light${i + 1}`));
      if (c.circulationPump) acc.push('circ pump');
      if (c.blower) acc.push(`blower (${c.blower})`);
      if (c.mister) acc.push('mister');
      c.aux.forEach((a, i) => a && acc.push(`aux${i + 1}`));
      log('  accessories: ' + (acc.join(', ') || 'none detected'));
    }
    if (state.filter) {
      const f = state.filter;
      log(`  filter 1: ${String(f.cycle1.startHour).padStart(2, '0')}:${String(f.cycle1.startMinute).padStart(2, '0')} for ${f.cycle1.durationMin}min` +
          `   filter 2: ${f.cycle2.enabled ? 'on' : 'off'} ${String(f.cycle2.startHour).padStart(2, '0')}:${String(f.cycle2.startMinute).padStart(2, '0')} for ${f.cycle2.durationMin}min`);
    }
    log('─'.repeat(48));
  }, 4000);
}
function actWithVerify(bytes, desc, check) {
  enqueue(bytes, desc);
  pending = { desc, check, deadline: Date.now() + 12000 };
  setTimeout(() => {
    if (pending && pending.deadline <= Date.now()) {
      log('✗ no confirming status echo for:', pending.desc, '— check that we are the only 0x0A client / wiring.');
      pending = null;
    }
  }, 12500);
}
function toggleLight() {
  if (!state.status) return log('· no status yet — wait for the monitor to lock on first.');
  const before = state.status.lights[0];
  actWithVerify(P.encodeToggleItem(P.ITEM.light1), `toggle light1 (was ${before ? 'on' : 'off'})`, (s) => s.lights[0] !== before);
}
function togglePump() {
  if (!state.status) return log('· no status yet.');
  const before = state.status.pumps[0];
  actWithVerify(P.encodeToggleItem(P.ITEM.pump1), `toggle pump1 (was ${before})`, (s) => s.pumps[0] !== before);
}
function nudgeTemp(delta) {
  const s = state.status;
  if (!s || s.targetTemp == null) return log('· no target temperature yet.');
  const newTarget = s.targetTemp + delta;
  const raw = s.tempScale === 'C' ? Math.round(newTarget * 2) : newTarget;
  actWithVerify(P.encodeSetTargetTemp(raw), `set target ${newTarget}°${s.tempScale}`, (st) => st.targetTemp === newTarget);
}

// ---- menu ----
function help() {
  log([
    '', '── commands ' + '─'.repeat(36),
    '  m  monitor snapshot + counters',
    '  c  confirm model + accessories (sends config requests)',
    '  1  toggle Light',
    '  2  toggle Pump 1',
    '  +  target temp +1°     -  target temp -1°',
    '  h  help          q / Ctrl-C  quit',
    '─'.repeat(48), '',
  ].join('\n'));
}
function snapshot() {
  log(`counters: frames=${counters.frames} status=${counters.status} ready=${counters.ready} ` +
      `info=${counters.info} config=${counters.config} filter=${counters.filter} unknown=${counters.unknown} noise-bytes=${counters.noise}`);
  if (!state.status) log('  (no Status decoded yet — if this stays at 0, check A/B wiring; try swapping RS-485 ±)');
}

const ACTIONS = {
  m: { fn: snapshot, safe: true },
  c: { fn: confirmModel, safe: true },
  h: { fn: help, safe: true },
  '?': { fn: help, safe: true },
  '1': { fn: toggleLight, safe: false, label: 'toggle the spa LIGHT' },
  '2': { fn: togglePump, safe: false, label: 'toggle spa PUMP 1' },
  '+': { fn: () => nudgeTemp(1), safe: false, label: 'raise target temp +1°' },
  '=': { fn: () => nudgeTemp(1), safe: false, label: 'raise target temp +1°' },
  '-': { fn: () => nudgeTemp(-1), safe: false, label: 'lower target temp -1°' },
};

function onKey(str, key) {
  if (key && (key.name === 'c' && key.ctrl)) return quit();
  const ch = str;
  if (confirm) { // awaiting y/N
    const proceed = ch && ch.toLowerCase() === 'y';
    const c = confirm; confirm = null;
    if (proceed) c.action(); else log('· cancelled.');
    return;
  }
  if (ch === 'q') return quit();
  const a = ACTIONS[ch];
  if (!a) return;
  if (a.safe) return a.fn();
  confirm = { action: a.fn };
  log(`⚠ ${a.label} — this affects the real spa. Press "y" to send, any other key to cancel.`);
}

function quit() {
  log('bye.');
  try { port.close(); } catch {}
  process.exit(0);
}

// ---- open port ----
const port = new SerialPort({ path: PATH, baudRate: BAUD, dataBits: 8, parity: 'none', stopBits: 1 }, (err) => {
  if (err) {
    console.error(`\nCannot open ${PATH}: ${err.message}`);
    console.error('Hints: is the adapter plugged in? is it /dev/ttyUSB0 (see `ls /dev/ttyUSB*`)? are you in the "dialout" group (`id -nG`)?');
    process.exit(1);
  }
  console.log(`Balboa RS-485 tester — ${PATH} @ ${BAUD} 8N1`);
  console.log('Listening (read-only). Press "h" for commands, "c" to confirm the model, "q" to quit.\n');
  help();
  renderLive();
  // no-data watchdog
  setInterval(() => { if (Date.now() - lastDataAt > 4000) renderLive(); }, 2000);
});
port.on('data', (chunk) => { lastDataAt = Date.now(); rx = Buffer.concat([rx, chunk]); drainFrames(); });
port.on('error', (e) => log('serial error:', e.message));
port.on('close', () => log('port closed.'));

// ---- keyboard ----
readline.emitKeypressEvents(process.stdin);
if (process.stdin.isTTY) process.stdin.setRawMode(true);
process.stdin.on('keypress', onKey);
process.on('SIGINT', quit);
