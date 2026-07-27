'use strict';
// Pure spa state machine — no MQTT, no clock of its own. Everything it needs is
// passed in, so the whole behaviour model is testable without a broker.

const HEAT_RATE_C_PER_MIN = 0.5;    // while the heater is running
const COOL_RATE_C_PER_MIN = 0.1;    // ambient loss otherwise
const HEAT_HYSTERESIS_C = 0.5;      // avoids chattering around the setpoint
const MIN_TEMP_C = 10;
const MAX_TEMP_C = 40;

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

const cToF = (c) => (c * 9) / 5 + 32;
const fToC = (f) => ((f - 32) * 5) / 9;

// Render a Celsius value in the spa's own scale, for display.
const inScale = (c, scale) => (scale === 'celsius' ? c : cToF(c));
const scaleUnit = (scale) => (scale === 'celsius' ? 'C' : 'F');

// Parse a temperature the user typed. Accepts an explicit unit ("96F", "35.6 C")
// and otherwise interprets the bare number in the spa's current scale — which is
// what someone reading a Fahrenheit panel will naturally type. Returns Celsius,
// or null if unparseable.
function parseTempToC(input, scale) {
  const m = String(input).trim().match(/^(-?\d+(?:\.\d+)?)\s*°?\s*([CF])?$/i);
  if (!m) return null;
  const v = parseFloat(m[1]);
  const unit = m[2] ? m[2].toUpperCase() : (scale === 'celsius' ? 'C' : 'F');
  return unit === 'C' ? v : fToC(v);
}

function createState() {
  return {
    currentTempC: 35.6,          // ~96 F, matching the real spa when we read it
    targetTempC: 26.7,           // ~80 F
    heatingMode: 'ready',        // ready | rest
    tempRange: 'high',           // high | low
    tempScale: 'fahrenheit',     // display hint only; the wire is always Celsius
    heating: false,
    priming: false,
    jetsSpeed: 0,                // 0 off, 1 low, 2 high
    light: false,
    hold: false,
    circPump: false,
    filter1Running: false,
    filter1StartHour: 20,
    filter1DurationMin: 120,
    notification: '',
    busConnected: true,
    model: 'CNBP501X',
    firmware: 'V36.0',
  };
}

// Is the wall-clock time inside filter cycle 1?
function filterCycleActive(s, date) {
  const minsNow = date.getHours() * 60 + date.getMinutes();
  const start = s.filter1StartHour * 60;
  const end = start + s.filter1DurationMin;
  if (end <= 24 * 60) return minsNow >= start && minsNow < end;
  return minsNow >= start || minsNow < end - 24 * 60;   // wraps midnight
}

// The heater only runs in ready mode, and only below the hysteresis band.
function shouldHeat(s) {
  if (s.heatingMode === 'rest') return false;
  return s.currentTempC < s.targetTempC - HEAT_HYSTERESIS_C;
}

// Advance the model by dtSeconds. Returns a NEW state; never mutates the input.
function tick(state, dtSeconds, date) {
  const s = { ...state };
  s.heating = shouldHeat(s);
  s.filter1Running = filterCycleActive(s, date);
  // Circulation runs during the filter cycle, and whenever the heater is on.
  s.circPump = s.filter1Running || s.heating;

  const mins = dtSeconds / 60;
  if (s.heating) {
    s.currentTempC += HEAT_RATE_C_PER_MIN * mins;
    // Don't overshoot the setpoint.
    if (s.currentTempC > s.targetTempC) s.currentTempC = s.targetTempC;
  } else {
    s.currentTempC -= COOL_RATE_C_PER_MIN * mins;
  }
  s.currentTempC = clamp(s.currentTempC, MIN_TEMP_C, MAX_TEMP_C);
  return s;
}

// Apply one command. `key` is a logical name, not a topic, so the transport can
// change without touching the model. Unknown keys and unparseable payloads are
// ignored rather than throwing — a simulator should not die on bad input.
function applyCommand(state, key, payload) {
  const s = { ...state };
  const p = String(payload).trim();
  const on = /^(ON|TRUE|1)$/i.test(p);
  const off = /^(OFF|FALSE|0)$/i.test(p);
  const num = parseFloat(p);

  switch (key) {
    // HA always sends Celsius on this topic, per the discovery config.
    case 'target_temperature':
      if (Number.isFinite(num)) s.targetTempC = clamp(num, MIN_TEMP_C, MAX_TEMP_C);
      break;
    // Test hooks: jump the water to a value instead of waiting for it to drift.
    // Accept "35.6", "96F", "35.6C" — bare numbers use the spa's current scale.
    case 'sim_current_temperature': {
      const c = parseTempToC(p, s.tempScale);
      if (c !== null) s.currentTempC = clamp(c, MIN_TEMP_C, MAX_TEMP_C);
      break;
    }
    case 'sim_target_temperature': {
      const c = parseTempToC(p, s.tempScale);
      if (c !== null) s.targetTempC = clamp(c, MIN_TEMP_C, MAX_TEMP_C);
      break;
    }
    case 'light':
      s.light = on ? true : off ? false : !s.light;      // TOGGLE support
      break;
    case 'hold':
      s.hold = on ? true : off ? false : !s.hold;
      break;
    case 'jets':
      if (off) s.jetsSpeed = 0;
      else if (on) s.jetsSpeed = s.jetsSpeed === 0 ? 1 : s.jetsSpeed;
      break;
    case 'jets_speed':
      if (Number.isFinite(num)) s.jetsSpeed = clamp(Math.round(num), 0, 2);
      break;
    case 'heating_mode':
      if (p === 'ready' || p === 'rest') s.heatingMode = p;
      break;
    case 'temperature_range':
      if (p === 'high' || p === 'low') s.tempRange = p;
      break;
    case 'temperature_scale':
      if (p === 'celsius' || p === 'fahrenheit') s.tempScale = p;
      break;
    case 'filter1_start_hour':
      if (Number.isFinite(num)) s.filter1StartHour = clamp(Math.round(num), 0, 23);
      break;
    case 'filter1_duration':
      if (Number.isFinite(num)) s.filter1DurationMin = clamp(Math.round(num), 0, 1440);
      break;
    // Accepted and ignored, mirroring the real device: the spa heater always
    // follows the setpoint, so "off" is not separately actionable.
    case 'climate_mode':
      break;
    case 'clear_notification':
      s.notification = '';
      break;
    case 'normal_operation':
      s.priming = false;
      break;
    // Test hook: force the bus "down" to exercise the dashboard's stale path.
    case 'sim_bus_connected':
      s.busConnected = on ? true : off ? false : !s.busConnected;
      break;
    default:
      break;
  }
  return s;
}

module.exports = {
  createState, tick, applyCommand, filterCycleActive, shouldHeat,
  cToF, fToC, inScale, scaleUnit, parseTempToC,
  HEAT_RATE_C_PER_MIN, COOL_RATE_C_PER_MIN, HEAT_HYSTERESIS_C, MIN_TEMP_C, MAX_TEMP_C,
};
