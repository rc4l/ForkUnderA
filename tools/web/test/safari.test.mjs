import { test } from 'node:test';
import assert from 'node:assert/strict';
import { WATCH, safariFaults, withWatch } from '../safari.mjs';

test('the watch goes in before any of the page runs', () => {
  assert.equal(withWatch('<head><title>x</title></head>'), `<head>\n${WATCH}<title>x</title></head>`);
});

test('a run that started, loaded, heard a click and logged nothing has no faults', () => {
  const good = { up: true, maps: { MAP01: 'Level MAP01 - Kills', MAP02: 'Level MAP02 - Kills' }, audio: ['running'], errors: [], crashed: false };
  assert.deepEqual(safariFaults(good), []);
});

test('each thing that can go wrong is named', () => {
  assert.deepEqual(safariFaults({ up: false }), ['the engine never started']);
  const bad = { up: true, maps: { MAP02: 'Unknown' }, audio: ['suspended'], errors: ['fatal thing'], crashed: true };
  assert.deepEqual(safariFaults(bad), ['MAP02 did not load: Unknown', 'the sound did not start after a click (suspended)', 'logged: fatal thing', 'the page showed its error panel']);
});
