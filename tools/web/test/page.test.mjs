import { test } from 'node:test';
import assert from 'node:assert/strict';
import { HEAR_CONSOLE, inPage, pause, repeatedSounds, soakInPage } from '../page.mjs';

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

test('page code hears the console until told to stop and reads the newest JSON line it said', async () => {
  const lines = [];
  const original = console.log;
  console.log = (...a) => lines.push(a.join(' '));
  try {
    const read = new Function(`${HEAR_CONSOLE}
      console.log('[1]'); console.log('{"pass":false}'); console.log('{"pass":true}');
      unhear();
      console.log('after');
      return { said, verdict: lastJson('{"pass"', {}), none: lastJson('nothing', 'none') };`)();
    assert.deepEqual(read.said, ['[1]', '{"pass":false}', '{"pass":true}']);
    assert.deepEqual(read.verdict, { pass: true });
    assert.equal(read.none, 'none');
    assert.deepEqual(lines, ['[1]', '{"pass":false}', '{"pass":true}', 'after']);
  } finally { console.log = original; }
});

test('a soak in the page runs each cycle\'s steps, waits for the title of each level one starts, and marks after the cycle', () => {
  const reload = soakInPage('reload', 3, 100);
  assert.match(reload, /const steps = \[\{"text":"map MAP01","map":"MAP01"\}\];/);
  assert.match(reload, /if \(step\.map\) await started\(from, step\.map\.toLowerCase\(\) \+ ' - '\);/);
  assert.match(reload, /i < 3;/);
  assert.match(reload, /fua_soak verdict 1 json/);
  assert.match(reload, /window\.gc\(\)/);
  assert.match(soakInPage('reload', 1, 10, 'MAP11'), /"text":"map MAP11","map":"MAP11"/);
  assert.match(soakInPage('map MAP02|map MAP01', 1, 10), /\[\{"text":"map MAP02","map":"MAP02"\},\{"text":"map MAP01","map":"MAP01"\}\]/);
  assert.match(soakInPage('vid_setmode 640 480', 1, 10), /\[\{"text":"vid_setmode 640 480","map":null\}\]/);
  const resize = soakInPage('check: resize 900x700|check: resize 1280x900', 2, 10);
  assert.match(resize, /if \(step\.text\.startsWith\('check: '\)\) console\.log\(step\.text\);\s+else Module\.fuaCommand\(step\.text\);/);
  assert.match(resize, /"text":"check: resize 900x700","map":null/);
});
