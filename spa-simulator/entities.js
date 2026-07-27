'use strict';
// Single source of truth for the simulator's MQTT surface: which entities exist,
// what they publish, and which commands they accept. Mirrors what the ESPHome
// gateway actually publishes (verified against the live device 2026-07-27).
//
// NOTE: ESPHome publishes *text* sensors under the `sensor` component type, not
// `text_sensor`. Getting this wrong yields entities that never receive data.

const DEVICE_ID = 'balboa-spa-sim';

const device = (state) => ({
  identifiers: [DEVICE_ID],
  name: 'Balboa Spa (Simulated)',
  manufacturer: 'Balboa Water Group',
  model: `${state.model} [simulated]`,
  sw_version: state.firmware,
});

// component, object_id, how to render state, optional command key, discovery bits.
const ENTITIES = [
  // --- climate: the primary control ---
  { component: 'climate', objectId: 'spa', name: 'Spa Sim',
    climate: true,
    disc: (t) => ({
      modes: ['off', 'heat'],
      mode_stat_t: `${t}/climate/spa/mode/state`,
      mode_cmd_t: `${t}/climate/spa/mode/command`,
      curr_temp_t: `${t}/climate/spa/current_temperature/state`,
      temp_stat_t: `${t}/climate/spa/target_temperature/state`,
      temp_cmd_t: `${t}/climate/spa/target_temperature/command`,
      act_t: `${t}/climate/spa/action/state`,
      min_temp: 10, max_temp: 40, temp_step: 0.5, temp_unit: 'C',
    }),
    states: (t, s) => [
      [`${t}/climate/spa/mode/state`, 'heat'],
      [`${t}/climate/spa/action/state`, s.heating ? 'heating' : 'idle'],
      [`${t}/climate/spa/current_temperature/state`, s.currentTempC.toFixed(1)],
      [`${t}/climate/spa/target_temperature/state`, s.targetTempC.toFixed(1)],
    ],
    commands: (t) => [
      [`${t}/climate/spa/target_temperature/command`, 'target_temperature'],
      // The real device accepts mode commands but ignores them — the heater
      // always follows the setpoint. Subscribe so HA doesn't sit optimistic.
      [`${t}/climate/spa/mode/command`, 'climate_mode'],
    ],
  },

  // --- fan: jets (2-speed) ---
  { component: 'fan', objectId: 'spa_jets', name: 'Spa Sim Jets',
    disc: (t) => ({
      stat_t: `${t}/fan/spa_jets/state`,
      cmd_t: `${t}/fan/spa_jets/command`,
      pct_stat_t: undefined,
      speed_range_min: 1, speed_range_max: 2,
      pr_mode_stat_t: undefined,
    }),
    extraDisc: (t) => ({
      // ESPHome-style discrete speed level topics
      pct_cmd_t: undefined,
    }),
    states: (t, s) => [
      [`${t}/fan/spa_jets/state`, s.jetsSpeed > 0 ? 'ON' : 'OFF'],
      [`${t}/fan/spa_jets/speed_level/state`, String(s.jetsSpeed)],
    ],
    commands: (t) => [
      [`${t}/fan/spa_jets/command`, 'jets'],
      [`${t}/fan/spa_jets/speed_level/command`, 'jets_speed'],
    ],
  },

  // --- switches ---
  { component: 'switch', objectId: 'spa_light', name: 'Spa Sim Light',
    states: (t, s) => [[`${t}/switch/spa_light/state`, s.light ? 'ON' : 'OFF']],
    commands: (t) => [[`${t}/switch/spa_light/command`, 'light']] },
  { component: 'switch', objectId: 'spa_hold', name: 'Spa Sim Hold',
    states: (t, s) => [[`${t}/switch/spa_hold/state`, s.hold ? 'ON' : 'OFF']],
    commands: (t) => [[`${t}/switch/spa_hold/command`, 'hold']] },

  // --- selects ---
  { component: 'select', objectId: 'spa_heating_mode', name: 'Spa Sim Heating Mode',
    disc: () => ({ options: ['ready', 'rest'] }),
    states: (t, s) => [[`${t}/select/spa_heating_mode/state`, s.heatingMode]],
    commands: (t) => [[`${t}/select/spa_heating_mode/command`, 'heating_mode']] },
  { component: 'select', objectId: 'spa_temperature_range', name: 'Spa Sim Temperature Range',
    disc: () => ({ options: ['high', 'low'] }),
    states: (t, s) => [[`${t}/select/spa_temperature_range/state`, s.tempRange]],
    commands: (t) => [[`${t}/select/spa_temperature_range/command`, 'temperature_range']] },
  { component: 'select', objectId: 'spa_temperature_scale', name: 'Spa Sim Temperature Scale',
    disc: () => ({ options: ['fahrenheit', 'celsius'] }),
    states: (t, s) => [[`${t}/select/spa_temperature_scale/state`, s.tempScale]],
    commands: (t) => [[`${t}/select/spa_temperature_scale/command`, 'temperature_scale']] },

  // --- numeric sensors (Celsius on the wire, like the real device) ---
  { component: 'sensor', objectId: 'spa_current_temperature', name: 'Spa Sim Current Temperature',
    disc: () => ({ unit_of_meas: '°C', dev_cla: 'temperature', stat_cla: 'measurement', sug_dsp_prc: 1 }),
    states: (t, s) => [[`${t}/sensor/spa_current_temperature/state`, s.currentTempC.toFixed(1)]] },
  { component: 'sensor', objectId: 'spa_target_temperature', name: 'Spa Sim Target Temperature',
    disc: () => ({ unit_of_meas: '°C', dev_cla: 'temperature', stat_cla: 'measurement', sug_dsp_prc: 1 }),
    states: (t, s) => [[`${t}/sensor/spa_target_temperature/state`, s.targetTempC.toFixed(1)]] },

  // --- text sensors: component type is `sensor`, NOT `text_sensor` ---
  { component: 'sensor', objectId: 'spa_model', name: 'Spa Sim Model',
    states: (t, s) => [[`${t}/sensor/spa_model/state`, s.model]] },
  { component: 'sensor', objectId: 'spa_firmware_version', name: 'Spa Sim Firmware Version',
    states: (t, s) => [[`${t}/sensor/spa_firmware_version/state`, s.firmware]] },
  { component: 'sensor', objectId: 'spa_notification', name: 'Spa Sim Notification',
    states: (t, s) => [[`${t}/sensor/spa_notification/state`, s.notification]] },

  // --- binary sensors ---
  { component: 'binary_sensor', objectId: 'spa_heating', name: 'Spa Sim Heating',
    states: (t, s) => [[`${t}/binary_sensor/spa_heating/state`, s.heating ? 'ON' : 'OFF']] },
  { component: 'binary_sensor', objectId: 'spa_priming', name: 'Spa Sim Priming',
    states: (t, s) => [[`${t}/binary_sensor/spa_priming/state`, s.priming ? 'ON' : 'OFF']] },
  { component: 'binary_sensor', objectId: 'spa_filter_cycle_1_running', name: 'Spa Sim Filter Cycle 1 Running',
    states: (t, s) => [[`${t}/binary_sensor/spa_filter_cycle_1_running/state`, s.filter1Running ? 'ON' : 'OFF']] },
  { component: 'binary_sensor', objectId: 'spa_circulation_pump', name: 'Spa Sim Circulation Pump',
    states: (t, s) => [[`${t}/binary_sensor/spa_circulation_pump/state`, s.circPump ? 'ON' : 'OFF']] },
  { component: 'binary_sensor', objectId: 'spa_bus_connected', name: 'Spa Sim Bus Connected',
    disc: () => ({ dev_cla: 'connectivity' }),
    states: (t, s) => [[`${t}/binary_sensor/spa_bus_connected/state`, s.busConnected ? 'ON' : 'OFF']],
    // Hidden test hook — lets the dashboard's stale path be exercised on demand.
    commands: (t) => [[`${t}/binary_sensor/spa_bus_connected/command`, 'sim_bus_connected']] },

  // --- simulator-only test hooks (no HA entity; command topics only) ---
  { component: 'sim', objectId: 'set_current_temperature', name: null, noDiscovery: true,
    states: () => [],
    commands: (t) => [[`${t}/sim/current_temperature/command`, 'sim_current_temperature']] },
  { component: 'sim', objectId: 'set_target_temperature', name: null, noDiscovery: true,
    states: () => [],
    commands: (t) => [[`${t}/sim/target_temperature/command`, 'sim_target_temperature']] },

  // --- numbers ---
  { component: 'number', objectId: 'spa_filter_1_start_hour', name: 'Spa Sim Filter 1 Start Hour',
    disc: () => ({ min: 0, max: 23, step: 1 }),
    states: (t, s) => [[`${t}/number/spa_filter_1_start_hour/state`, String(s.filter1StartHour)]],
    commands: (t) => [[`${t}/number/spa_filter_1_start_hour/command`, 'filter1_start_hour']] },
  { component: 'number', objectId: 'spa_filter_1_duration', name: 'Spa Sim Filter 1 Duration',
    disc: () => ({ min: 0, max: 1440, step: 15, unit_of_meas: 'min' }),
    states: (t, s) => [[`${t}/number/spa_filter_1_duration/state`, String(s.filter1DurationMin)]],
    commands: (t) => [[`${t}/number/spa_filter_1_duration/command`, 'filter1_duration']] },

  // --- buttons (command-only) ---
  { component: 'button', objectId: 'spa_clear_notification', name: 'Spa Sim Clear Notification',
    states: () => [],
    commands: (t) => [[`${t}/button/spa_clear_notification/command`, 'clear_notification']] },
  { component: 'button', objectId: 'spa_normal_operation', name: 'Spa Sim Normal Operation',
    states: () => [],
    commands: (t) => [[`${t}/button/spa_normal_operation/command`, 'normal_operation']] },
];

// Build the HA discovery payload for one entity.
function discoveryFor(e, topicPrefix, state) {
  const t = topicPrefix;
  const base = {
    name: e.name,
    uniq_id: `spasim_${e.objectId}`,
    avty_t: `${t}/status`,
    pl_avail: 'online',
    pl_not_avail: 'offline',
    dev: device(state),
  };
  if (!e.climate) {
    const first = e.states ? e.states(t, state)[0] : null;
    if (first) base.stat_t = first[0];
    const cmds = e.commands ? e.commands(t) : [];
    if (cmds.length) base.cmd_t = cmds[0][0];
  }
  const extra = e.disc ? e.disc(t) : {};
  const merged = { ...base, ...extra };
  // strip undefined so the payload stays clean
  Object.keys(merged).forEach((k) => merged[k] === undefined && delete merged[k]);
  return merged;
}

const discoveryTopic = (e, discoveryPrefix) =>
  `${discoveryPrefix}/${e.component}/${DEVICE_ID}/${e.objectId}/config`;

// Entities that get a Home Assistant discovery config. Test hooks are excluded:
// they are command topics only, deliberately invisible in HA.
const discoverable = () => ENTITIES.filter((e) => !e.noDiscovery);

// All state topic/payload pairs for the current state.
function allStates(topicPrefix, state) {
  return ENTITIES.flatMap((e) => (e.states ? e.states(topicPrefix, state) : []));
}

// topic -> command key, for subscription routing.
function commandRoutes(topicPrefix) {
  const m = new Map();
  for (const e of ENTITIES) {
    if (!e.commands) continue;
    for (const [topic, key] of e.commands(topicPrefix)) m.set(topic, key);
  }
  return m;
}

module.exports = { ENTITIES, DEVICE_ID, discoveryFor, discoveryTopic, allStates, commandRoutes, discoverable };
