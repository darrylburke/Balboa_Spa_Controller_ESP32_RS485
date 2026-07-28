'use strict';
// Virtual Balboa spa over MQTT. Publishes the same surface the ESPHome gateway
// publishes, under its own prefix and device id, so a dashboard built against it
// works unchanged against real hardware.
//
//   node spa-sim.js [--prefix spa-sim] [--fast 60] [--clean]
//
// Credentials come from ../secrets.yaml so they match the real device.
const mqtt = require('mqtt');
const fs = require('fs');
const path = require('path');
const model = require('./model');
const E = require('./entities');

const args = process.argv.slice(2);
const getArg = (n, d) => { const i = args.indexOf(n); return i >= 0 && args[i + 1] ? args[i + 1] : d; };
const PREFIX = getArg('--prefix', 'spa-sim');
const DISCOVERY_PREFIX = getArg('--discovery-prefix', 'homeassistant');
const FAST = Number(getArg('--fast', '1'));      // clock multiplier
const CLEAN = args.includes('--clean');
const TICK_MS = 2000;

const SECRETS = getArg('--secrets', path.join(__dirname, '..', 'secrets.yaml'));
function readSecrets(file) {
  const out = {};
  try {
    for (const line of fs.readFileSync(file, 'utf8').split('\n')) {
      const m = line.match(/^([a-z_]+):\s*(.*)$/i);
      if (m) out[m[1]] = m[2].trim().replace(/^["']|["']$/g, '');
    }
  } catch (e) {
    console.error(`Cannot read ${file}: ${e.message}`);
    process.exit(1);
  }
  return out;
}
const s = readSecrets(SECRETS);
const BROKER = getArg('--broker', s.mqtt_broker);
const PORT = Number(getArg('--port', '1883'));

let state = model.createState();
const published = new Map();   // topic -> last payload, so we only publish changes

console.log(`Balboa spa SIMULATOR — ${BROKER}:${PORT}`);
console.log(`  state prefix : ${PREFIX}`);
console.log(`  discovery    : ${DISCOVERY_PREFIX}/<component>/${E.DEVICE_ID}/...`);
console.log(`  clock        : ${FAST}x`);
if (CLEAN) console.log('  MODE         : --clean (remove all retained topics, then exit)');
console.log('');

const client = mqtt.connect(`mqtt://${BROKER}:${PORT}`, {
  username: s.mqtt_username,
  password: s.mqtt_password,
  reconnectPeriod: 3000,
  will: CLEAN ? undefined : { topic: `${PREFIX}/status`, payload: 'offline', qos: 1, retain: true },
});

function pub(topic, payload, retain = true) {
  if (!CLEAN && published.get(topic) === payload) return;   // unchanged
  published.set(topic, payload);
  client.publish(topic, payload, { qos: 1, retain });
}

// Remove every retained topic this simulator created, so it leaves no trace.
function clean(done) {
  const topics = new Set();
  for (const e of E.discoverable()) topics.add(E.discoveryTopic(e, DISCOVERY_PREFIX));
  for (const [t] of E.allStates(PREFIX, state)) topics.add(t);
  topics.add(`${PREFIX}/status`);
  let pending = topics.size;
  console.log(`Clearing ${pending} retained topics...`);
  for (const t of topics) {
    client.publish(t, '', { qos: 1, retain: true }, () => { if (--pending === 0) done(); });
  }
}

client.on('connect', () => {
  console.log(`✓ connected to ${BROKER}`);

  if (CLEAN) {
    clean(() => { console.log('✓ cleaned — the simulated device will disappear from HA.'); client.end(); });
    return;
  }

  // Discovery first, so HA knows the entities before state arrives.
  for (const e of E.discoverable()) {
    client.publish(E.discoveryTopic(e, DISCOVERY_PREFIX),
      JSON.stringify(E.discoveryFor(e, PREFIX, state)), { qos: 1, retain: true });
  }
  console.log(`· published ${E.discoverable().length} discovery configs`);

  pub(`${PREFIX}/status`, 'online');

  const routes = E.commandRoutes(PREFIX);
  for (const topic of routes.keys()) client.subscribe(topic, { qos: 1 });
  console.log(`· subscribed to ${routes.size} command topics`);
  console.log('\nSimulator running. Ctrl-C to stop (--clean removes it from HA).\n');

  publishState();
  setInterval(step, TICK_MS);
});

// Console output follows the spa's own scale, so it matches what the panel shows.
function fmtTemps() {
  const u = model.scaleUnit(state.tempScale);
  const c = model.inScale(state.currentTempC, state.tempScale);
  const t = model.inScale(state.targetTempC, state.tempScale);
  return `(${c.toFixed(1)}→${t.toFixed(1)}°${u})`;
}

function publishState() {
  for (const [topic, payload] of E.allStates(PREFIX, state)) pub(topic, payload);
}

function step() {
  const before = state;
  state = model.tick(state, (TICK_MS / 1000) * FAST, new Date());
  if (before.heating !== state.heating) {
    console.log(`· heater ${state.heating ? 'ON' : 'OFF'} ${fmtTemps()}`);
  }
  if (before.filter1Running !== state.filter1Running) {
    console.log(`· filter cycle ${state.filter1Running ? 'STARTED' : 'ENDED'}`);
  }
  publishState();
}

client.on('message', (topic, payload) => {
  const key = E.commandRoutes(PREFIX).get(topic);
  if (!key) return;
  const p = payload.toString();
  const prev = state;
  state = model.applyCommand(state, key, p);
  const tempChanged = prev.currentTempC !== state.currentTempC || prev.targetTempC !== state.targetTempC
                      || prev.tempScale !== state.tempScale;
  console.log(`← ${key} = ${p}${tempChanged ? '  ' + fmtTemps() : ''}`);
  publishState();          // echo immediately, like the real device
});

client.on('error', (e) => console.error('mqtt error:', e.message));

process.on('SIGINT', () => {
  console.log('\nstopping — publishing offline (use --clean to remove from HA entirely)');
  client.publish(`${PREFIX}/status`, 'offline', { qos: 1, retain: true }, () => {
    client.end(true, () => process.exit(0));
  });
});
