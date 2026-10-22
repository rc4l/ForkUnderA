import { test } from 'node:test';
import assert from 'node:assert/strict';
import { HEAR_CONSOLE, WATCH_LEVEL, WATCH_FRAMES, WATCH_GPU_CALLS, STARTED, GRAB_FRAME, inPage, pause, repeatedSounds, serveData, soakInPage, swapWgsl, framePacing, gpuCallRates, longestGap } from '../page.mjs';

// [rc4l] STARTED run against a stand-in page: its error panel, its Module, and whether it is isolated.
function runStarted({ errorShown = false, errorText = '', module = {}, isolated = true }) {
  const elements = { error: { hidden: !errorShown }, 'error-text': { textContent: errorText } };
  const document = { getElementById: (id) => elements[id] };
  return new Function('document', 'window', 'Module', 'crossOriginIsolated', `return ${STARTED};`)(document, { Module: module }, module, isolated);
}

test('page code reports the page\'s error, or the engine answering, with whether the page is isolated', async () => {
  assert.deepEqual(await runStarted({ errorShown: true, errorText: 'Could not fetch freedoom2.wad (404)', isolated: false }), { isolated: false, error: 'Could not fetch freedoom2.wad (404)' });
  assert.deepEqual(await runStarted({ module: { fuaCommand() {} } }), { isolated: true, error: null });
  const module = {};
  setTimeout(() => { module.fuaCommand = () => {}; }, 150);
  assert.deepEqual(await runStarted({ module }), { isolated: true, error: null });
});

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
  assert.match(soakInPage('reload', 9, 10, 'MAP01', 3), /fua_soak verdict 3 json/);
  assert.match(soakInPage('map MAP02|map MAP01', 1, 10), /\[\{"text":"map MAP02","map":"MAP02"\},\{"text":"map MAP01","map":"MAP01"\}\]/);
  assert.match(soakInPage('vid_setmode 640 480', 1, 10), /\[\{"text":"vid_setmode 640 480","map":null\}\]/);
  const resize = soakInPage('check: resize 900x700|check: resize 1280x900', 2, 10);
  assert.match(resize, /if \(step\.text\.startsWith\('check: '\)\) console\.log\(step\.text\);\s+else Module\.fuaCommand\(step\.text\);/);
  assert.match(resize, /"text":"check: resize 900x700","map":null/);
});

// [rc4l] A slow adapter was still melting the screen when a mark was asked for, so the wipe's two screen copies read as memory kept; the engine takes such a mark once the wipe ends.
test('a soak in the page waits for each mark the engine takes late, and names what grew from the first judged mark', async () => {
  let wipe = 0, marks = 0, kept = 0;
  const timers = [];
  const later = (ms, f) => timers.push(setTimeout(f, ms));
  const Module = {
    fuaGpuLedger: { tagText: () => `fua scene VB\t${1000 + kept}\nfua wipe start\t${wipe}` },
    fuaCommand(text) {
      if (text === 'map MAP01') { wipe = 4000000; kept += 100000; later(250, () => { wipe = 0; }); console.log('MAP01 - entryway'); }
      if (text === 'fua_soak mark') later(300, () => console.log(`telemetry: soak mark ${++marks}`));
      if (text.startsWith('fua_soak verdict')) console.log(`{"pass":true,"marks":${marks}}`);
    },
  };
  globalThis.window = { Module };
  globalThis.Module = Module;
  const log = console.log;
  console.log = () => {};
  try {
    const result = await eval(soakInPage('reload', 3, 10, 'MAP01', 1));
    assert.deepEqual(result, { pass: true, marks: 3, kept: ['fua scene VB +98 KB'] });
  } finally {
    console.log = log;
    for (const t of timers) clearTimeout(t);
    delete globalThis.window;
    delete globalThis.Module;
  }
});

// [rc4l] Sounds a tic on the game thread started all started and were silent, which counting starts never saw; the level watch hears the output itself.
test('the level watch splices one analyser per context in front of the output and averages what it carries', () => {
  class Node { constructor(ctx) { this.context = ctx; this.links = []; } connect(target) { this.links.push(target); return target; } }
  class Destination extends Node {}
  const ctx = { state: 'running', currentTime: 1.234, createAnalyser: () => Object.assign(new Node(ctx), { getFloatTimeDomainData: (buf) => buf.fill(0.5) }) };
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
  assert.equal(win.fuaLevel.peak(), 0.5);
  assert.deepEqual(win.fuaLevel.contexts(), [{ state: 'running', time: 1.23 }]);
  win.fuaLevel.reset();
  assert.equal(win.fuaLevel.mean(), 0);
  assert.equal(win.fuaLevel.peak(), 0);
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

test('a file served to the page joins its data list and comes back as its bytes, and every other fetch goes through', async () => {
  const asked = [];
  const window = { fetch: async (url) => { asked.push(url); return new Response(url.endsWith('files.json') ? '[{"name":"freedoom2.wad","size":3}]' : 'real'); } };
  new Function('window', serveData('map.wad', Buffer.from([1, 2, 250])))(window);
  assert.deepEqual(await (await window.fetch('data/files.json')).json(), [{ name: 'freedoom2.wad', size: 3 }, { name: 'map.wad', size: 3 }]);
  assert.deepEqual([...new Uint8Array(await (await window.fetch('data/map.wad')).arrayBuffer())], [1, 2, 250]);
  assert.equal(await (await window.fetch('data/freedoom2.wad')).text(), 'real');
  assert.deepEqual(asked, ['data/files.json', 'data/freedoom2.wad']);
});

// [rc4l] GRAB_FRAME against a stand-in page whose screenshot hands the canvas's toBlob a download of the frame's bytes.
function grabbing(frames) {
  const commands = [], downloads = [];
  class Canvas { toBlob(cb, type) { cb(new Blob([Uint8Array.from(frames.shift())], { type })); } }
  const Module = { canvas: new Canvas(), fuaCommand: (c) => { commands.push(c); if (c === 'screenshot') Module.canvas.toBlob(() => downloads.push(c), 'image/png'); } };
  const grabFrame = new Function('Module', `${GRAB_FRAME} return grabFrame;`)(Module);
  return { grabFrame, Module, commands, downloads };
}

test('a grabbed frame is the next one drawn, as base64, taken quietly instead of its download', async () => {
  const { grabFrame, Module, commands, downloads } = grabbing([[137, 80, 78, 71], [1, 2]]);
  assert.equal(await grabFrame(), Buffer.from([137, 80, 78, 71]).toString('base64'));
  assert.equal(await grabFrame(), Buffer.from([1, 2]).toString('base64'));
  assert.deepEqual(commands.slice(0, 4), ['clear', 'screenshot_quiet 1', 'screenshot', 'screenshot_quiet 0']);
  assert.deepEqual(downloads, []);
  assert.equal(Object.hasOwn(Module.canvas, 'toBlob'), false);
});

test('a WGSL swap rewrites every shader the page makes and leaves the rest of its description', () => {
  const made = [];
  class GPUDevice { createShaderModule(desc) { made.push(desc); return 'module'; } }
  new Function('GPUDevice', swapWgsl('1.0f / gl_FragCoord.w', 'vPixelPos.w'))(GPUDevice);
  assert.equal(new GPUDevice().createShaderModule({ label: 'scene', code: 'let d = (1.0f / gl_FragCoord.w); let e = (1.0f / gl_FragCoord.w);' }), 'module');
  assert.deepEqual(made, [{ label: 'scene', code: 'let d = (vPixelPos.w); let e = (vPixelPos.w);' }]);
});

test('a soak asked for heaps waits on a snapshot at its first judged mark and at its end, and one not asked takes none', () => {
  const heap = soakInPage('reload', 9, 10, 'MAP01', 3, { heap: true });
  assert.match(heap, /if \(i === 3\) \{ base = ledger\(\); await heapAt\('base'\); \}/);
  assert.match(heap, /await heapAt\('end'\);\s+held\('end'\);/);
  assert.match(heap, /window\.fuaHeapTaken !== label/);
  assert.doesNotMatch(soakInPage('reload', 9, 10, 'MAP01', 3), /await heapAt/);
});

test('a soak asks the driver to collect before each mark, and collects itself when no driver answers', () => {
  const js = soakInPage('reload', 3, 100);
  assert.match(js, /console\.log\('check: gc ' \+ n\);/);
  assert.match(js, /window\.fuaGcTaken !== n/);
  assert.match(js, /if \(window\.fuaGcTaken !== n\) for \(let k = 0; k < 2 && window\.gc; k\+\+\)/);
});

test('the gpu calls look gives each busiest kind a second, and says when there were none', () => {
  const by = { 'bind group decal': { calls: 1300, bytes: 0 }, 'write Upload memory page': { calls: 80, bytes: 100 * 1048576 }, 'copy to fua dynamic VB': { calls: 80, bytes: 20 * 1048576 } };
  assert.equal(gpuCallRates({ from: 0, by }, 10000, 2), 'gpu calls a second: bind group decal 130 | write Upload memory page 8 (10.00 MB)');
  assert.equal(gpuCallRates({ from: 5000, by: {} }, 6000), 'gpu calls: none');
  assert.match(WATCH_GPU_CALLS, /GPUDevice\.prototype\.createBindGroup = function/);
  assert.match(WATCH_GPU_CALLS, /if \(window\.GPUDevice\)/);
});

test('longestGap finds the longest wait between frames and where it began', () => {
  assert.deepEqual(longestGap([]), { ms: 0, at: 0 });
  assert.deepEqual(longestGap([100]), { ms: 0, at: 0 });
  assert.deepEqual(longestGap([100, 116, 132, 8132, 8148]), { ms: 8000, at: 32 });
});
