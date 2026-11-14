'use strict';
// Pure planning/observation rules. No MQTT connection, credentials or retries.
function jetsCommand(on, speed) {
  const next = !on ? 1 : speed >= 2 ? 0 : 2;
  return next === 0
    ? { topic: 'fan/spa_jets/command', payload: 'OFF', next }
    : { topic: 'fan/spa_jets/speed_level/command', payload: String(next), next };
}
function eligibleObservation(pending, topic, retained, now) {
  return Boolean(pending && !retained && topic === pending.expectedTopic &&
    now >= pending.started && now <= pending.deadline);
}
module.exports = { jetsCommand, eligibleObservation };
