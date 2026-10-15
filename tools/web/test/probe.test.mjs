import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parseRun, report, runScript } from '../probe.mjs';

test('a run is console lines with waits between them', () => {
  assert.deepEqual(parseRun('god; give all ;wait 1000; +attack;'), [{ command: 'god' }, { command: 'give all' }, { wait: 1000 }, { command: '+attack' }]);
  assert.deepEqual(parseRun(''), []);
});

test('a run becomes page code that sends each command and waits, after the engine starts', () => {
  const js = runScript(parseRun('playsound "brain/cube"; wait 50'));
  assert.match(js, /Module\.fuaCommand\("playsound \\"brain\/cube\\""\);/);
  assert.match(js, /setTimeout\(r, 50\)/);
  assert.match(js, /window\.fuaSounds\.slice\(from\)/);
});

test('the report lists the sounds, flags the repeats, and gives the textures and the errors', () => {
  const lines = report({ sounds: [{ id: 2, len: 1.313, t: 7.7 }, { id: 3, len: 0.071, t: 7.8 }, { id: 2, len: 1.313, t: 9.1 }], heldBefore: 6, heldAfter: 6 }, ['[log] WebGPU error: Out of Memory', '[log] other']);
  assert.deepEqual(lines, [
    'sound #2 (1.313 s) at 7.700',
    'sound #2 (1.313 s) at 9.100',
    '1 stream piece(s) under 0.1 s',
    'AGAIN: sound #2 (1.313 s) started 2 times, at 7.700, 9.100',
    'screen textures held: 6 before, 6 after',
    '[log] WebGPU error: Out of Memory',
  ]);
});
