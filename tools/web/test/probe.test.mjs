import { test } from 'node:test';
import assert from 'node:assert/strict';
import { parseRun, queryMap, report, runScript, soakLines } from '../probe.mjs';

test('a run is console lines with waits between them', () => {
  assert.deepEqual(parseRun('god; give all ;wait 1000; +attack;'), [{ command: 'god' }, { command: 'give all' }, { wait: 1000 }, { command: '+attack' }]);
  assert.deepEqual(parseRun(''), []);
});

test('a run becomes page code that sends each command and waits, after the engine starts', () => {
  const js = runScript(parseRun('playsound "brain/cube"; wait 50'));
  assert.match(js, /Module\.fuaCommand\("playsound \\"brain\/cube\\""\);/);
  assert.match(js, /setTimeout\(r, 50\)/);
  assert.match(js, /window\.fuaSounds\.slice\(from\)/);
  assert.match(js, /unhear\(\);\s+return \{ said,/);
});

test('a soak reloads the map the query starts on and prints the engine verdict', () => {
  assert.equal(queryMap('iwad=doom2.wad&file=sunder.wad&map=MAP11'), 'MAP11');
  assert.equal(queryMap('iwad=doom2.wad'), 'MAP01');
  assert.deepEqual(soakLines({ report: 'SOAK FAIL: 7 marks, 30 counters judged, 1 grew\n  gpu/process_bytes grew 18.0 MB a cycle (90.0 MB over 5 cycles)\n' }),
    ['SOAK FAIL: 7 marks, 30 counters judged, 1 grew', '  gpu/process_bytes grew 18.0 MB a cycle (90.0 MB over 5 cycles)']);
  assert.deepEqual(soakLines({}), ['the soak gave no verdict']);
});

test('the report lists the sounds, flags the repeats, and gives the textures and the errors', () => {
  const lines = report({ said: ['gpu/process_bytes 184.0 MB'], sounds: [{ id: 2, len: 1.313, t: 7.7 }, { id: 3, len: 0.071, t: 7.8 }, { id: 2, len: 1.313, t: 9.1 }], heldBefore: 6, heldAfter: 6 }, ['[log] WebGPU error: Out of Memory', '[log] other']);
  assert.deepEqual(lines, [
    '> gpu/process_bytes 184.0 MB',
    'sound #2 (1.313 s) at 7.700',
    'sound #2 (1.313 s) at 9.100',
    '1 stream piece(s) under 0.1 s',
    'AGAIN: sound #2 (1.313 s) started 2 times, at 7.700, 9.100',
    'screen textures held: 6 before, 6 after',
    '[log] WebGPU error: Out of Memory',
  ]);
});
