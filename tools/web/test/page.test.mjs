import { test } from 'node:test';
import assert from 'node:assert/strict';
import { HEAR_CONSOLE, WATCH_LEVEL, WATCH_FRAMES, inPage, pause, repeatedSounds, soakInPage, framePacing } from '../page.mjs';

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

// [rc4l] Sounds a tic on the game thread started all started and were silent, which counting starts never saw; the level watch hears the output itself.
test('the level watch splices one analyser per context in front of the output and averages what it carries', () => {
  class Node { constructor(ctx) { this.context = ctx; this.links = []; } connect(target) { this.links.push(target); return target; } }
  class Destination extends Node {}
  const ctx = { createAnalyser: () => Object.assign(new Node(ctx), { getFloatTimeDomainData: (buf) => buf.fill(0.5) }) };
  const ticks = [];
  const win = {};
  new Function('window', 'AudioNode', 'AudioDestinationNode', 'setInterval', WATCH_LEVEL)(win, Node, Destination, (fn) => ticks.push(fn));
  const out = new Destination(ctx);
  const a = new Node(ctx);
  const b = new Node(ctx);
  const gain = new Node(ctx);
  a.connect(out);
  b.connect(out);
  a.connect(gain);
  assert.equal(win.fuaLevel.taps.length, 1);
  const analyser = win.fuaLevel.taps[0].an;
  assert.deepEqual(a.links, [analyser, gain]);
  assert.deepEqual(b.links, [analyser]);
  assert.deepEqual(analyser.links, [out]);
  assert.equal(win.fuaLevel.mean(), 0);
  ticks[0]();
  ticks[0]();
  assert.equal(win.fuaLevel.mean(), 0.5);
  win.fuaLevel.reset();
  assert.equal(win.fuaLevel.mean(), 0);
});

test('page code asks a command and gets back only what it printed', async () => {
  const original = console.log;
  console.log = () => {};
  const Module = { fuaCommand: (t) => console.log(`ran ${t}`) };
  try {
    const run = new Function('Module', `return (async () => { ${HEAR_CONSOLE}
      console.log('before');
      const got = await ask('currentpos', 5);
      unhear();
      return got; })();`);
    assert.deepEqual(await run(Module), ['ran currentpos']);
  } finally { console.log = original; }
});

test('frame pacing gives the median, p99 and longest gap and counts the hitches', () => {
  const times = [0];
  for (let i = 1; i <= 100; i++) times.push(times[i - 1] + (i === 50 ? 60 : i === 80 ? 25 : 4));
  assert.equal(framePacing(times), 'frames: 100, interval median 4.00 p99 60.00 max 60.0 ms, over 20 ms 2, over 50 ms 1');
  assert.equal(framePacing([5]), 'frames: none drawn');
  assert.equal(framePacing([]), 'frames: none drawn');
});

test('the frame watch keeps each frame time the browser hands it', () => {
  const pending = [];
  const window = {};
  new Function('window', 'requestAnimationFrame', WATCH_FRAMES)(window, (f) => pending.push(f));
  for (const t of [16, 33, 50]) pending.shift()(t);
  assert.deepEqual(window.fuaFrames, [16, 33, 50]);
  assert.equal(pending.length, 1);
});
