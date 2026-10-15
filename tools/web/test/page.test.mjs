import { test } from 'node:test';
import assert from 'node:assert/strict';
import { inPage, pause, repeatedSounds } from '../page.mjs';

test('page code waits for the engine to start before it runs', () => {
  const js = inPage('return 1;');
  assert.match(js, /^\(async \(\) => \{ while \(!\(window\.Module && Module\.fuaCommand\)\)/);
  assert.match(js, /return 1; \}\)\(\)$/);
});

test('a pause is page code that waits its milliseconds', () => {
  assert.equal(pause(250), 'await new Promise((r) => setTimeout(r, 250));');
});

test('a sound started more than once is found by its buffer', () => {
  const sounds = [{ id: 1, len: 1.655, t: 6.4 }, { id: 2, len: 1.313, t: 7.7 }, { id: 2, len: 1.313, t: 9.1 }];
  assert.deepEqual(repeatedSounds(sounds), [{ id: 2, len: 1.313, times: [7.7, 9.1] }]);
  assert.deepEqual(repeatedSounds([]), []);
});
