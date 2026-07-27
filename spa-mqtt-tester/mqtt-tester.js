'use strict';
// Balboa spa bench tester over MQTT — the counterpart to ../spa-serial-tester,
// exercising the gateway's published surface instead of the RS-485 bus.
//
// Credentials come from ../secrets.yaml so they live in exactly one place and
// always match what the firmware itself uses.
const mqtt = require('mqtt');
const fs = require('fs');
const path = require('path');
const readline = require('node:readline');

// ---- args ----
const args = process.argv.slice(2);
const getArg = (n, d) => { const i = args.indexOf(n); return i >= 0 && args[i + 1] ? args[i + 1] : d; };
const AUTO_YES = args.includes('--yes') || args.includes('-y');
const PREFIX = getArg('--prefix', 'spa');
const SECRETS = getArg('--secrets', path.join(__dirname, '..', 'secrets.yaml'));

// Flat `key: "value"` file — no YAML dependency needed.
function readSecrets(file) {
  const out = {};
  let text;
  try { text = fs.readFileSync(file, 'utf8'); } catch (e) {
    console.error(`Cannot read ${file}: ${e.message}`);
    console.error('Pass --secrets <file>, or --broker/--user/--pass explicitly.');
    process.exit(1);
  }
  for (const line of text.split('\n')) {
    const m = line.match(/^([a-z_]+):\s*(.*)$/i);
    if (m) out[m[1]] = m[2].trim().replace(/^["']|["']$/g, '');
  }
  return out;
}
const s = fs.existsSync(SECRETS) ? readSecrets(SECRETS) : {};
const BROKER = getArg('--broker', s.mqtt_broker);
const USER = getArg('--user', s.mqtt_username);
const PASS = getArg('--pass', s.mqtt_password);
const PORT = Number(getArg('--port', '1883'));
if (!BROKER) { console.error('No broker: set mqtt_broker in secrets.yaml or pass --broker.'); process.exit(1); }

// ---- state ----
const T = {};                    // topic (relative to prefix) -> payload
const counters = { msgs: 0, cmds: 0 };
let pending = null;              // { desc, check, deadline }
let confirm = null;
let connected = false;
const rel = (t) => t.startsWith(PREFIX + '/') ? t.slice(PREFIX.length + 1) : t;

// ---- output ----
let lastLive = '';
function log(...a) { process.stdout.write('\r\x1b[K' + a.join(' ') + '\n'); process.stdout.write(lastLive); }

const num = (v) => { const n = parseFloat(v); return Number.isFinite(n) ? n : null; };
const scaleIsF = () => (T['select/spa_temperature_scale/state'] || 'fahrenheit') === 'fahrenheit';
// Everything on the wire is Celsius; display in the spa's own scale.
const toDisplay = (c) => (c == null ? null : (scaleIsF() ? c * 9 / 5 + 32 : c));
const toCelsius = (d) => (scaleIsF() ? (d - 32) * 5 / 9 : d);
const displayStep = () => (scaleIsF() ? 5 / 9 : 0.5);   // 1 °F or 0.5 °C
const unit = () => (scaleIsF() ? 'F' : 'C');
const fmt = (v, dp = 0) => (v == null ? '--' : v.toFixed(dp));

function renderLive() {
  const online = T['status'] === 'online';
  const bus = T['binary_sensor/spa_bus_connected/state'];
  let line;
  if (!connected) {
    line = 'connecting to broker…';
  } else if (!online) {
    line = `⚠ spa OFFLINE (status=${T['status'] ?? '?'}) — values below are retained/stale`;
  } else {
    const cur = toDisplay(num(T['climate/spa/current_temperature/state']));
    const tgt = toDisplay(num(T['climate/spa/target_temperature/state']));
    const jets = T['fan/spa_jets/state'] === 'ON' ? `jets:${T['fan/spa_jets/speed_level/state'] === '2' ? 'hi' : 'lo'}` : 'jets:off';
    const light = T['switch/spa_light/state'] === 'ON' ? 'light:on' : 'light:off';
    const act = T['climate/spa/action/state'] ?? '?';
    const mode = T['select/spa_heating_mode/state'] ?? '?';
    const busTag = bus === undefined ? '' : (bus === 'ON' ? '' : ' ⚠BUS-DOWN');
    line = `${fmt(cur)}°${unit()} → ${fmt(tgt)}° | ${act} | ${mode} | ${jets} | ${light}${busTag}`;
  }
  lastLive = '\r\x1b[K' + line;
  process.stdout.write(lastLive);
}

// ---- publishing ----
function publish(topic, payload, desc) {
  const full = `${PREFIX}/${topic}`;
  // retain:false is essential — a retained command would re-fire on every
  // gateway reconnect and move real equipment unprompted.
  client.publish(full, String(payload), { qos: 1, retain: false }, (err) => {
    if (err) log('✗ publish failed:', err.message);
    else { counters.cmds++; log(`→ ${desc}  [${full} = ${payload}]`); }
  });
}
function actWithVerify(topic, payload, desc, check, ms = 12000) {
  publish(topic, payload, desc);
  pending = { desc, check, deadline: Date.now() + ms };
  setTimeout(() => {
    if (pending && pending.deadline <= Date.now()) {
      log(`✗ no confirming state for: ${pending.desc} — is the spa online and the bus connected?`);
      pending = null;
    }
  }, ms + 250);
}
function checkPending() {
  if (pending && pending.check()) { log('✓ confirmed:', pending.desc); pending = null; }
}

// ---- actions ----
const guard = () => {
  if (!connected) { log('· not connected to the broker yet.'); return false; }
  if (T['status'] !== 'online') { log('· spa is OFFLINE — refusing to send (it would queue against a dead device).'); return false; }
  if (T['binary_sensor/spa_bus_connected/state'] === 'OFF') { log('· RS-485 bus is DOWN — the gateway cannot reach the spa.'); return false; }
  return true;
};

function toggleLight() {
  const before = T['switch/spa_light/state'];
  actWithVerify('switch/spa_light/command', 'TOGGLE', `toggle light (was ${before ?? '?'})`,
    () => T['switch/spa_light/state'] !== before);
}
function cycleJets() {
  const on = T['fan/spa_jets/state'] === 'ON';
  const lvl = parseInt(T['fan/spa_jets/speed_level/state'] || '0', 10);
  const next = !on ? 1 : (lvl >= 2 ? 0 : lvl + 1);   // off -> low -> high -> off
  if (next === 0) {
    actWithVerify('fan/spa_jets/command', 'OFF', 'jets off', () => T['fan/spa_jets/state'] === 'OFF');
  } else {
    publish('fan/spa_jets/command', 'ON', `jets on`);
    actWithVerify('fan/spa_jets/speed_level/command', next, `jets speed ${next}`,
      () => T['fan/spa_jets/speed_level/state'] === String(next));
  }
}
function nudgeTemp(dir) {
  const cur = num(T['climate/spa/target_temperature/state']);
  if (cur == null) return log('· no target temperature known yet.');
  const target = toDisplay(cur) + dir * displayStep();
  const celsius = toCelsius(target);
  actWithVerify('climate/spa/target_temperature/command', celsius.toFixed(2),
    `set target ${fmt(target)}°${unit()}`,
    () => { const n = num(T['climate/spa/target_temperature/state']); return n != null && Math.abs(n - celsius) < 0.3; });
}
function toggleHeatingMode() {
  const before = T['select/spa_heating_mode/state'];
  if (!before) return log('· heating mode unknown yet.');
  const next = before === 'rest' ? 'ready' : 'rest';
  actWithVerify('select/spa_heating_mode/command', next, `heating mode ${before} -> ${next}`,
    () => T['select/spa_heating_mode/state'] === next);
}
function pressButton(which) {
  publish(`button/spa_${which}/command`, 'PRESS', `press ${which}`);
}

function askLine(q, cb) {
  const raw = process.stdin.isTTY && process.stdin.isRaw;
  if (raw) process.stdin.setRawMode(false);
  const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
  rl.question(q, (a) => { rl.close(); if (raw) process.stdin.setRawMode(true); cb(a); });
}
function setFilter() {
  const sh = T['number/spa_filter_1_start_hour/state'];
  const du = T['number/spa_filter_1_duration/state'];
  log(`· current filter 1: start hour ${sh ?? '?'}, duration ${du ?? '?'} min  (PERSISTENT setting)`);
  askLine('  new start hour (0-23, blank = keep): ', (a) => {
    askLine('  new duration in minutes (blank = keep): ', (b) => {
      const send = () => {
        if (a.trim()) actWithVerify('number/spa_filter_1_start_hour/command', parseInt(a, 10),
          `filter 1 start hour ${a.trim()}`, () => T['number/spa_filter_1_start_hour/state'] === String(parseInt(a, 10)));
        if (b.trim()) actWithVerify('number/spa_filter_1_duration/command', parseInt(b, 10),
          `filter 1 duration ${b.trim()}`, () => T['number/spa_filter_1_duration/state'] === String(parseInt(b, 10)));
        if (!a.trim() && !b.trim()) log('· nothing to change.');
      };
      if (AUTO_YES) { log('· auto-confirmed (--yes)'); return send(); }
      confirm = { action: send };
      log('  Press "y" to write it, any other key to cancel.');
    });
  });
}

// ---- reporting ----
function snapshot() {
  log(`counters: messages=${counters.msgs} commands=${counters.cmds}  topics=${Object.keys(T).length}`);
  log(`  availability : ${T['status'] ?? '(none)'}`);
  log(`  bus connected: ${T['binary_sensor/spa_bus_connected/state'] ?? '(entity not present — old firmware?)'}`);
  const stale = T['status'] !== 'online';
  if (stale) log('  ⚠ values are RETAINED from the last time the gateway was online.');
}
function dumpAll() {
  log('── all known topics ' + '─'.repeat(28));
  for (const k of Object.keys(T).sort()) log(`  ${k} = ${T[k]}`);
  log('─'.repeat(48));
}
function showConfig() {
  log('── spa ' + '─'.repeat(42));
  // NB: ESPHome publishes text sensors under the `sensor` component type, not
  // `text_sensor` — verified against a live device.
  log(`  model      : ${T['sensor/spa_model/state'] ?? '?'}`);
  log(`  firmware   : ${T['sensor/spa_firmware_version/state'] ?? '?'}`);
  log(`  notification: ${T['sensor/spa_notification/state'] || '(none)'}`);
  log(`  scale      : ${T['select/spa_temperature_scale/state'] ?? '?'}   range: ${T['select/spa_temperature_range/state'] ?? '?'}`);
  log(`  filter 1   : start ${T['number/spa_filter_1_start_hour/state'] ?? '?'}h for ${T['number/spa_filter_1_duration/state'] ?? '?'} min`);
  log(`  heating    : ${T['binary_sensor/spa_heating/state'] ?? '?'}   priming: ${T['binary_sensor/spa_priming/state'] ?? '?'}   filter running: ${T['binary_sensor/spa_filter_cycle_1_running/state'] ?? '?'}`);
  log('─'.repeat(48));
}
function help() {
  log([
    '', '── commands ' + '─'.repeat(36),
    '  m  counters + availability      d  dump every known topic',
    '  c  spa model / config summary',
    '  1  toggle Light                 2  cycle Jets (off/low/high)',
    '  +  target temp +1°              -  target temp -1°',
    '  r  heating mode READY <-> REST',
    '  f  set filter 1 start/duration (persistent)',
    '  n  press Clear Notification     o  press Normal Operation',
    '  h  help                         q / Ctrl-C  quit',
    AUTO_YES ? '  (--yes active: acting commands send with no confirmation)'
             : '  (1 2 r + - n o and f ask for "y" first; --yes skips that)',
    '─'.repeat(48), '',
  ].join('\n'));
}

const ACTIONS = {
  m: { fn: snapshot, safe: true },
  d: { fn: dumpAll, safe: true },
  c: { fn: showConfig, safe: true },
  h: { fn: help, safe: true },
  '?': { fn: help, safe: true },
  f: { fn: setFilter, safe: true },          // prompts + confirms itself
  1: { fn: toggleLight, safe: false, label: 'toggle the spa LIGHT' },
  2: { fn: cycleJets, safe: false, label: 'cycle spa JETS' },
  r: { fn: toggleHeatingMode, safe: false, label: 'toggle heating mode READY/REST' },
  '+': { fn: () => nudgeTemp(1), safe: false, label: 'raise target temp' },
  '=': { fn: () => nudgeTemp(1), safe: false, label: 'raise target temp' },
  '-': { fn: () => nudgeTemp(-1), safe: false, label: 'lower target temp' },
  n: { fn: () => pressButton('clear_notification'), safe: false, label: 'press CLEAR NOTIFICATION' },
  o: { fn: () => pressButton('normal_operation'), safe: false, label: 'press NORMAL OPERATION' },
};

function onKey(str, key) {
  if (key && key.ctrl && key.name === 'c') return quit();
  if (confirm) {
    const go = str && str.toLowerCase() === 'y';
    const c = confirm; confirm = null;
    if (go) c.action(); else log('· cancelled.');
    return;
  }
  if (str === 'q') return quit();
  const a = ACTIONS[str];
  if (!a) return;
  if (a.safe) return a.fn();
  if (!guard()) return;
  if (AUTO_YES) { log(`⚠ ${a.label} — auto-confirmed (--yes)`); return a.fn(); }
  confirm = { action: a.fn };
  log(`⚠ ${a.label} — this affects the real spa. Press "y" to send, any other key to cancel.`);
}
function quit() { log('bye.'); try { client.end(true); } catch {} process.exit(0); }

// ---- connect ----
console.log(`Balboa MQTT tester — ${BROKER}:${PORT} as ${USER || '(anonymous)'}, prefix "${PREFIX}"`);
console.log(AUTO_YES
  ? '⚠ --yes: acting commands send IMMEDIATELY, no confirmation.\n'
  : 'Acting commands ask for "y" before sending.\n');

const client = mqtt.connect(`mqtt://${BROKER}:${PORT}`, {
  username: USER, password: PASS, reconnectPeriod: 3000, connectTimeout: 10000,
  // Clean session: we rely on retained state to resync, not on a queued backlog.
  clean: true,
});

client.on('connect', () => {
  connected = true;
  log(`✓ connected to ${BROKER}`);
  client.subscribe(`${PREFIX}/#`, { qos: 1 }, (err) => {
    if (err) log('✗ subscribe failed:', err.message);
    else log(`· subscribed to ${PREFIX}/#  (retained state arrives immediately)`);
  });
  help();
  renderLive();
});
client.on('reconnect', () => { connected = false; log('· reconnecting…'); renderLive(); });
client.on('error', (e) => log('✗ mqtt error:', e.message));
client.on('close', () => { connected = false; renderLive(); });

client.on('message', (topic, payload) => {
  counters.msgs++;
  const r = rel(topic);
  if (r === 'debug') return;                 // ESPHome log stream, too noisy
  T[r] = payload.toString();
  checkPending();
  renderLive();
});

setInterval(renderLive, 2000);
readline.emitKeypressEvents(process.stdin);
if (process.stdin.isTTY) process.stdin.setRawMode(true);
process.stdin.on('keypress', onKey);
process.on('SIGINT', quit);
