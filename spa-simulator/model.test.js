'use strict';
// Behaviour model tests — no MQTT, no real clock.
const { test } = require('node:test');
const assert = require('node:assert');
const m = require('./model');
const E = require('./entities');

const at = (h, min = 0) => new Date(2026, 6, 27, h, min, 0);

test('heater engages below the hysteresis band and stops at setpoint', () => {
  let s = m.createState();
  s.currentTempC = 20; s.targetTempC = 30; s.heatingMode = 'ready';
  s = m.tick(s, 60, at(12));
  assert.strictEqual(s.heating, true);
  assert.ok(s.currentTempC > 20, 'should warm up');

  // Just below setpoint but inside hysteresis -> no heating
  s.currentTempC = 29.8; s.targetTempC = 30;
  s = m.tick(s, 60, at(12));
  assert.strictEqual(s.heating, false);
});

test('heating never overshoots the setpoint', () => {
  let s = m.createState();
  s.currentTempC = 29; s.targetTempC = 30;
  s = m.tick(s, 60 * 60, at(12));      // a full hour at 0.5C/min would blow past
  assert.ok(s.currentTempC <= 30 + 1e-9, `overshot to ${s.currentTempC}`);
});

test('rest mode suppresses heating entirely', () => {
  let s = m.createState();
  s.currentTempC = 20; s.targetTempC = 35; s.heatingMode = 'rest';
  s = m.tick(s, 60, at(12));
  assert.strictEqual(s.heating, false);
  assert.ok(s.currentTempC < 20, 'should cool, not heat');
});

test('temperature is clamped to the valid range', () => {
  let s = m.createState();
  s.currentTempC = 10.05; s.heatingMode = 'rest';
  s = m.tick(s, 60 * 60 * 5, at(12));
  assert.ok(s.currentTempC >= m.MIN_TEMP_C, `fell below min: ${s.currentTempC}`);
});

test('filter cycle runs inside its window, including across midnight', () => {
  const s = m.createState();
  s.filter1StartHour = 20; s.filter1DurationMin = 120;   // 20:00-22:00
  assert.strictEqual(m.filterCycleActive(s, at(21)), true);
  assert.strictEqual(m.filterCycleActive(s, at(19)), false);
  assert.strictEqual(m.filterCycleActive(s, at(23)), false);

  const w = { ...s, filter1StartHour: 23, filter1DurationMin: 180 };  // 23:00-02:00
  assert.strictEqual(m.filterCycleActive(w, at(23, 30)), true);
  assert.strictEqual(m.filterCycleActive(w, at(1)), true);
  assert.strictEqual(m.filterCycleActive(w, at(3)), false);
});

test('circulation pump follows filter cycle or heating', () => {
  let s = m.createState();
  s.filter1StartHour = 20; s.filter1DurationMin = 120;
  s.currentTempC = 35; s.targetTempC = 35;              // no heat demand
  s = m.tick(s, 1, at(21));
  assert.strictEqual(s.circPump, true, 'filter cycle should run the circ pump');
  s = m.tick(s, 1, at(12));
  assert.strictEqual(s.circPump, false);
});

test('commands apply, including TOGGLE semantics', () => {
  let s = m.createState();
  s.light = false;
  s = m.applyCommand(s, 'light', 'ON');    assert.strictEqual(s.light, true);
  s = m.applyCommand(s, 'light', 'OFF');   assert.strictEqual(s.light, false);
  s = m.applyCommand(s, 'light', 'TOGGLE'); assert.strictEqual(s.light, true);

  s = m.applyCommand(s, 'target_temperature', '38.5');
  assert.strictEqual(s.targetTempC, 38.5);
  s = m.applyCommand(s, 'heating_mode', 'rest');
  assert.strictEqual(s.heatingMode, 'rest');
  s = m.applyCommand(s, 'jets_speed', '2');
  assert.strictEqual(s.jetsSpeed, 2);
});

test('bad and unknown commands are ignored, not fatal', () => {
  const s0 = m.createState();
  assert.deepStrictEqual(m.applyCommand(s0, 'nonsense', 'x'), s0);
  const s1 = m.applyCommand(s0, 'target_temperature', 'not-a-number');
  assert.strictEqual(s1.targetTempC, s0.targetTempC);
  const s2 = m.applyCommand(s0, 'heating_mode', 'banana');
  assert.strictEqual(s2.heatingMode, s0.heatingMode);
});

test('target temperature is clamped to the visual range', () => {
  let s = m.createState();
  s = m.applyCommand(s, 'target_temperature', '999');
  assert.strictEqual(s.targetTempC, m.MAX_TEMP_C);
  s = m.applyCommand(s, 'target_temperature', '-50');
  assert.strictEqual(s.targetTempC, m.MIN_TEMP_C);
});

test('bus-connected test hook drives the stale path', () => {
  let s = m.createState();
  assert.strictEqual(s.busConnected, true);
  s = m.applyCommand(s, 'sim_bus_connected', 'OFF');
  assert.strictEqual(s.busConnected, false);
});

test('tick never mutates the input state', () => {
  const s = m.createState();
  const copy = { ...s };
  m.tick(s, 600, at(12));
  assert.deepStrictEqual(s, copy);
});

// --- entity surface ---

test('text sensors publish under the sensor component, not text_sensor', () => {
  const model_ = E.ENTITIES.find((e) => e.objectId === 'spa_model');
  assert.strictEqual(model_.component, 'sensor');
  assert.ok(!E.ENTITIES.some((e) => e.component === 'text_sensor'));
});

test('every state topic is namespaced under the sim prefix', () => {
  const st = E.allStates('spa-sim', m.createState());
  assert.ok(st.length > 0);
  for (const [topic] of st) assert.ok(topic.startsWith('spa-sim/'), topic);
});

test('discovery ids cannot collide with the real device', () => {
  const s = m.createState();
  for (const e of E.ENTITIES) {
    const d = E.discoveryFor(e, 'spa-sim', s);
    assert.ok(d.uniq_id.startsWith('spasim_'), d.uniq_id);
    assert.strictEqual(d.avty_t, 'spa-sim/status');
    assert.ok(E.discoveryTopic(e, 'ha').includes('balboa-spa-sim'));
  }
});

test('temperatures are published in Celsius, matching the real device', () => {
  const s = m.createState();
  s.currentTempC = 35.6;
  const st = new Map(E.allStates('spa-sim', s));
  assert.strictEqual(st.get('spa-sim/sensor/spa_current_temperature/state'), '35.6');
  assert.strictEqual(st.get('spa-sim/climate/spa/current_temperature/state'), '35.6');
  const disc = E.discoveryFor(E.ENTITIES.find((e) => e.objectId === 'spa_current_temperature'), 'spa-sim', s);
  assert.strictEqual(disc.unit_of_meas, '°C');
});

test('command routes cover every command topic exactly once', () => {
  const routes = E.commandRoutes('spa-sim');
  assert.ok(routes.get('spa-sim/switch/spa_light/command') === 'light');
  assert.ok(routes.get('spa-sim/climate/spa/target_temperature/command') === 'target_temperature');
  assert.ok(routes.size >= 10);
});

// --- temperature entry and scale-aware display ---

test('parseTempToC honours an explicit unit suffix', () => {
  assert.ok(Math.abs(m.parseTempToC('100F', 'celsius') - 37.78) < 0.01);
  assert.strictEqual(m.parseTempToC('35.6C', 'fahrenheit'), 35.6);
  assert.ok(Math.abs(m.parseTempToC('96 °F', 'celsius') - 35.56) < 0.01);
});

test('a bare number is read in the spa current scale', () => {
  // Someone reading a Fahrenheit panel types "100" and means 100F.
  assert.ok(Math.abs(m.parseTempToC('100', 'fahrenheit') - 37.78) < 0.01);
  assert.strictEqual(m.parseTempToC('37.8', 'celsius'), 37.8);
});

test('parseTempToC rejects junk rather than guessing', () => {
  assert.strictEqual(m.parseTempToC('hot', 'celsius'), null);
  assert.strictEqual(m.parseTempToC('', 'celsius'), null);
  assert.strictEqual(m.parseTempToC('12K', 'celsius'), null);
});

test('setting current temperature works in either scale and clamps', () => {
  let s = m.createState();               // starts in fahrenheit
  s = m.applyCommand(s, 'sim_current_temperature', '104');
  assert.ok(Math.abs(s.currentTempC - 40) < 0.01, `got ${s.currentTempC}`);
  s = m.applyCommand(s, 'sim_current_temperature', '20C');
  assert.strictEqual(s.currentTempC, 20);
  s = m.applyCommand(s, 'sim_current_temperature', '500F');
  assert.strictEqual(s.currentTempC, m.MAX_TEMP_C, 'must clamp');
});

test('setting target temperature drives the heater', () => {
  let s = m.createState();
  s = m.applyCommand(s, 'sim_current_temperature', '80F');
  s = m.applyCommand(s, 'sim_target_temperature', '104F');
  s = m.tick(s, 1, at(12));
  assert.strictEqual(s.heating, true, 'big setpoint gap should call for heat');
});

test('inScale renders Celsius state in the spa display scale', () => {
  assert.strictEqual(m.inScale(100, 'celsius'), 100);
  assert.ok(Math.abs(m.inScale(37.78, 'fahrenheit') - 100) < 0.05);
  assert.strictEqual(m.scaleUnit('fahrenheit'), 'F');
  assert.strictEqual(m.scaleUnit('celsius'), 'C');
});

test('the wire stays Celsius regardless of the display scale', () => {
  let s = m.createState();
  s = m.applyCommand(s, 'temperature_scale', 'fahrenheit');
  s.currentTempC = 35.6;
  const f = new Map(E.allStates('spa-sim', s));
  s = m.applyCommand(s, 'temperature_scale', 'celsius');
  const c = new Map(E.allStates('spa-sim', s));
  assert.strictEqual(f.get('spa-sim/sensor/spa_current_temperature/state'), '35.6');
  assert.strictEqual(c.get('spa-sim/sensor/spa_current_temperature/state'), '35.6');
});

test('test hooks are not exposed as HA entities', () => {
  const ids = E.discoverable().map((e) => e.objectId);
  assert.ok(!ids.includes('set_current_temperature'));
  assert.ok(E.commandRoutes('spa-sim').has('spa-sim/sim/current_temperature/command'));
});
