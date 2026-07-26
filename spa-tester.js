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
// Resume a channel the spa assigned us earlier instead of negotiating a new one.
// The controller polls an assigned channel until the spa is power-cycled, so
// reusing it across runs avoids leaving a trail of dead channels on the bus.
const RESUME = getArg('--channel', null);
// Skip the y/N confirmation on commands that move real equipment. Every such
// command still logs what it sent and still verifies against the spa's next
// status broadcast — this only removes the keypress, not the check.
const AUTO_YES = args.includes('--yes') || args.includes('-y');

// ---- state ----
let rx = Buffer.alloc(0);
const state = { status: null, info: null, config: null, filter: null };
const counters = { frames: 0, status: 0, ready: 0, readyOurs: 0, readyOther: 0, info: 0, config: 0, filter: 0, unknown: 0, noise: 0 };
const txQueue = [];          // { bytes, desc }
let pending = null;          // { desc, check(status)->bool, deadline }
let lastDataAt = 0;
let confirm = null;          // { prompt, action }
// Our bus channel. 0x00 = unregistered: we must stay silent until the controller
// assigns us one, and may then transmit ONLY in a Ready addressed to that channel.
let myId = 0x00;
let lastIdRequest = 0;

// ---- output helpers (live status line via \r, logs printed above it) ----
let lastLive = '';
function log(...a) { process.stdout.write('\r\x1b[K' + a.join(' ') + '\n'); process.stdout.write(lastLive); }
function renderLive() {
  const s = state.status;
  const ch = myId ? `ch:0x${myId.toString(16)}` : 'ch:--';
  let line;
  if (!s) {
    const age = lastDataAt ? Math.round((Date.now() - lastDataAt) / 1000) : null;
    line = lastDataAt ? `reading… ${ch} | ${counters.frames} frames, ${counters.ready} ready (${age}s since last)` : 'waiting for data…';
  } else {
    const t = (v) => (v == null ? '--' : v);
    const pumps = s.pumps.map((p, i) => (p ? `p${i + 1}:${p === 2 ? 'hi' : 'lo'}` : null)).filter(Boolean).join(' ') || 'pumps:off';
    const lights = s.lights.map((l, i) => (l ? `light${i + 1}` : null)).filter(Boolean).join(' ') || 'light:off';
    line = `${ch} | ${t(s.currentTemp)}°${s.tempScale} → ${t(s.targetTemp)}°  | ${s.heating ? 'HEAT' : 'idle'} | ${s.heatingMode} | ${s.tempRange} | ${pumps} | ${lights} | ${String(s.hour).padStart(2, '0')}:${String(s.minute).padStart(2, '0')}`;
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
    case 'ready':
      counters.ready++;
      // Only OUR window. Transmitting in another client's slot collides with it.
      if (P.isReadyFor(f, myId)) { counters.readyOurs++; maybeSend(); }
      else counters.readyOther++;
      break;
    case 'id_assign': adoptChannel(m.channel); break;
    case 'new_client': if (P.isNewClientCTS(f)) requestChannel(); break;
    case 'nothing_to_send': case 'toggle': break;   // other clients' bus chatter
    default: counters.unknown++;
  }
}

// ---- channel negotiation ----
function requestChannel() {
  if (myId !== 0x00) return;
  if (Date.now() - lastIdRequest < 1500) return;   // don't spam the bus
  lastIdRequest = Date.now();
  port.write(P.encodeIdRequest(), (err) => {
    if (err) log('✗ ID request failed:', err.message);
    else log('· asked the controller for a channel…');
  });
}
function adoptChannel(ch) {
  if (myId !== 0x00) return;                        // already registered
  myId = ch;
  port.write(P.encodeIdAck(myId), (err) => {
    if (err) log('✗ ID ack failed:', err.message);
    else log(`✓ assigned channel 0x${myId.toString(16)} — we may now transmit in our own window`);
  });
  renderLive();
}

function maybeSend() {
  if (txQueue.length === 0) {
    port.write(P.encodeNothingToSend(myId));        // well-behaved client: answer every window
    return;
  }
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
function registered() {
  if (myId === 0x00) {
    log('· not registered on the bus yet — waiting for the controller to assign a channel. Try again in a moment.');
    return false;
  }
  return true;
}
function enqueue(bytes, desc) { txQueue.push({ bytes, desc }); log('· queued:', desc, '— sending in our next Ready window…'); }

function confirmModel() {
  if (!registered()) return;
  state.info = state.config = state.filter = null;
  enqueue(P.encodeControlConfigRequest(1, myId), 'request info (model/version)');
  enqueue(P.encodeControlConfigRequest(2, myId), 'request config (accessories)');
  enqueue(P.encodeControlConfigRequest(3, myId), 'request filter cycles');
  setTimeout(() => {
    log('── model / config ' + '─'.repeat(30));
    if (state.info) log(`  model: ${state.info.model}   firmware: ${state.info.version}`);
    else log(`  (no info response on channel 0x${myId.toString(16)} — the request may have been missed; try 'c' again)`);
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
  if (!registered()) return;
  if (!state.status) return log('· no status yet — wait for the monitor to lock on first.');
  const before = state.status.lights[0];
  actWithVerify(P.encodeToggleItem(P.ITEM.light1, myId), `toggle light1 (was ${before ? 'on' : 'off'})`, (s) => s.lights[0] !== before);
}
function togglePump() {
  if (!registered()) return;
  if (!state.status) return log('· no status yet.');
  const before = state.status.pumps[0];
  actWithVerify(P.encodeToggleItem(P.ITEM.pump1, myId), `toggle pump1 (was ${before})`, (s) => s.pumps[0] !== before);
}
// Ready <-> Rest, the same toggle the topside panel sends (item 0x51).
// One press = one toggle; from ready_in_rest it takes two to reach ready.
function toggleHeatingMode() {
  if (!registered()) return;
  if (!state.status) return log('· no status yet.');
  const before = state.status.heatingMode;
  actWithVerify(P.encodeToggleItem(P.ITEM.heating_mode, myId),
    `toggle heating mode (was ${before})`,
    (s) => s.heatingMode !== before);
}

function nudgeTemp(delta) {
  if (!registered()) return;
  const s = state.status;
  if (!s || s.targetTemp == null) return log('· no target temperature yet.');
  const newTarget = s.targetTemp + delta;
  const raw = s.tempScale === 'C' ? Math.round(newTarget * 2) : newTarget;
  actWithVerify(P.encodeSetTargetTemp(raw, myId), `set target ${newTarget}°${s.tempScale}`, (st) => st.targetTemp === newTarget);
}

// ---- filter cycles ----
// "HH:MM H:MM" -> {startHour, startMinute, durationMin}, or null if unparseable.
function parseCycle(str) {
  const m = String(str).trim().match(/^(\d{1,2}):(\d{2})\s+(\d{1,2}):(\d{2})$/);
  if (!m) return null;
  const [, sh, sm, dh, dm] = m.map(Number);
  if (sh > 23 || sm > 59 || dm > 59) return null;
  return { startHour: sh, startMinute: sm, durationMin: dh * 60 + dm };
}
const fmtCycle = (c) =>
  `${String(c.startHour).padStart(2, '0')}:${String(c.startMinute).padStart(2, '0')} for ` +
  `${Math.floor(c.durationMin / 60)}:${String(c.durationMin % 60).padStart(2, '0')}`;

// Prompt on a normal line-buffered stdin, then restore raw keypress mode.
function askLine(question, cb) {
  const wasRaw = process.stdin.isTTY && process.stdin.isRaw;
  if (wasRaw) process.stdin.setRawMode(false);
  const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
  rl.question(question, (answer) => {
    rl.close();
    if (wasRaw) process.stdin.setRawMode(true);
    cb(answer);
  });
}

function setFilterCycles() {
  if (!registered()) return;
  if (!state.filter) return log('· no filter cycles known yet — press "c" first.');
  const cur = state.filter;
  log(`· current  filter1: ${fmtCycle(cur.cycle1)}   filter2: ${cur.cycle2.enabled ? 'on' : 'off'} ${fmtCycle(cur.cycle2)}`);
  log('· format "HH:MM H:MM" = start time then duration. Blank keeps the current value.');
  askLine('  new filter1 (start dur): ', (a1) => {
    const c1 = a1.trim() ? parseCycle(a1) : cur.cycle1;
    if (!c1) { log('✗ could not parse — nothing sent.'); return; }
    askLine('  new filter2 (start dur, or "off"): ', (a2) => {
      let c2 = cur.cycle2;
      const t2 = a2.trim().toLowerCase();
      if (t2 === 'off') c2 = { ...cur.cycle2, enabled: false };
      else if (t2) {
        const p = parseCycle(a2);
        if (!p) { log('✗ could not parse — nothing sent.'); return; }
        c2 = { ...p, enabled: true };
      }
      const fc = { cycle1: c1, cycle2: c2 };
      log(`⚠ writing PERSISTENT setting — filter1: ${fmtCycle(c1)}   filter2: ${c2.enabled ? 'on' : 'off'} ${fmtCycle(c2)}`);
      log(`  (previous filter1: ${fmtCycle(cur.cycle1)}   filter2: ${cur.cycle2.enabled ? 'on' : 'off'} ${fmtCycle(cur.cycle2)})`);
      const send = () => {
        enqueue(P.encodeFilterCycles(fc, myId), 'set filter cycles');
        state.filter = null;
        enqueue(P.encodeControlConfigRequest(3, myId), 're-read filter cycles');
        setTimeout(() => {
          if (!state.filter) return log('✗ no filter-cycle response — press "c" to re-read.');
          const g = state.filter;
          const ok = g.cycle1.startHour === c1.startHour && g.cycle1.startMinute === c1.startMinute &&
                     g.cycle1.durationMin === c1.durationMin;
          log(`${ok ? '✓ confirmed' : '✗ mismatch'} — spa now reports filter1: ${fmtCycle(g.cycle1)}   ` +
              `filter2: ${g.cycle2.enabled ? 'on' : 'off'} ${fmtCycle(g.cycle2)}`);
        }, 5000);
      };
      if (AUTO_YES) { log('· auto-confirmed (--yes)'); return send(); }
      confirm = { action: send };
      log('  Press "y" to write it, any other key to cancel.');
    });
  });
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
    '  r  toggle heating mode (READY <-> REST)',
    '  f  set filter cycle start/duration (persistent — prompts for values)',
    '  h  help          q / Ctrl-C  quit',
    AUTO_YES ? '  (--yes active: 1 2 r + - and the f write send with no confirmation)'
             : '  (1 2 r + - and the f write ask for "y" first; --yes skips that)',
    '─'.repeat(48), '',
  ].join('\n'));
}
function snapshot() {
  log(`counters: frames=${counters.frames} status=${counters.status} ready=${counters.ready} ` +
      `(ours=${counters.readyOurs} others=${counters.readyOther}) ` +
      `info=${counters.info} config=${counters.config} filter=${counters.filter} unknown=${counters.unknown} noise-bytes=${counters.noise}`);
  log(`channel: ${myId ? '0x' + myId.toString(16) : 'unregistered (staying silent)'}`);
  if (!state.status) log('  (no Status decoded yet — if this stays at 0, check A/B wiring; try swapping RS-485 ±)');
}

const ACTIONS = {
  m: { fn: snapshot, safe: true },
  c: { fn: confirmModel, safe: true },
  h: { fn: help, safe: true },
  '?': { fn: help, safe: true },
  '1': { fn: toggleLight, safe: false, label: 'toggle the spa LIGHT' },
  '2': { fn: togglePump, safe: false, label: 'toggle spa PUMP 1' },
  r: { fn: toggleHeatingMode, safe: false, label: 'toggle heating mode READY <-> REST' },
  // Prompts for values and does its own confirmation, so it is 'safe' here.
  f: { fn: setFilterCycles, safe: true },
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
  if (AUTO_YES) {
    log(`⚠ ${a.label} — auto-confirmed (--yes)`);
    return a.fn();
  }
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
  if (RESUME) {
    const ch = parseInt(RESUME, 16);
    if (ch > 0 && ch <= 0x2f) {
      myId = ch;
      console.log(`Resuming bus channel 0x${myId.toString(16)} (no handshake).`);
    } else {
      console.log(`Ignoring --channel ${RESUME}: must be 01–2f hex.`);
    }
  }
  console.log('Listening (read-only). Press "h" for commands, "c" to confirm the model, "q" to quit.');
  console.log(AUTO_YES
    ? '⚠ --yes: equipment commands send IMMEDIATELY, and the "f" filter write\n'
      + '  (a PERSISTENT setting) is applied without confirmation too.\n'
    : 'Equipment commands and the "f" filter write ask for "y" before sending.\n');
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
