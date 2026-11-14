'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const { jetsCommand, eligibleObservation } = require('./command-plan');

test('one Jets action plans one command, without a preceding ON', () => {
  assert.deepEqual(jetsCommand(false, 1), { topic: 'fan/spa_jets/speed_level/command', payload: '1', next: 1 });
  assert.deepEqual(jetsCommand(true, 1), { topic: 'fan/spa_jets/speed_level/command', payload: '2', next: 2 });
  assert.deepEqual(jetsCommand(true, 2), { topic: 'fan/spa_jets/command', payload: 'OFF', next: 0 });
});
test('retained, unrelated and late observations cannot satisfy pending command', () => {
  const p = { expectedTopic: 'fan/spa_jets/state', started: 100, deadline: 200 };
  assert.equal(eligibleObservation(p, p.expectedTopic, false, 150), true);
  assert.equal(eligibleObservation(p, p.expectedTopic, true, 150), false);
  assert.equal(eligibleObservation(p, 'switch/spa_light/state', false, 150), false);
  assert.equal(eligibleObservation(p, p.expectedTopic, false, 201), false);
  assert.equal(eligibleObservation(p, p.expectedTopic, false, 99), false);
});
test('disconnect-cleared pending request is not revived on reconnect', () => {
  assert.equal(eligibleObservation(null, 'fan/spa_jets/state', false, 150), false);
});
