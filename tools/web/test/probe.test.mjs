import { test } from 'node:test';
import assert from 'node:assert/strict';
import { abLine, abShots, initFor, loadSave, parseRun, parseSwap, queryMap, report, runScript, soakLines, stallLine } from '../probe.mjs';
import { encodePNG } from '../../fuactl/src/pngcore.mjs';

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

// [rc4l] Seven one-off browser scripts each copied a Chrome harness to time frames or hear the output, for want of these two steps.
test('a run looks at the frame pacing and the audio level since its last look', () => {
  const steps = parseRun('god; wait 4000; frames; level; +attack; frames');
  assert.deepEqual(steps, [{ command: 'god' }, { wait: 4000 }, { look: 'frames' }, { look: 'level' }, { command: '+attack' }, { look: 'frames' }]);
  const js = runScript(steps);
  assert.match(js, /let framesFrom = window\.fuaFrames\.length;/);
  assert.match(js, /function framePacing\(times\)/);
  assert.equal(js.match(/framesFrom = window\.fuaFrames\.length;/g).length, 3);
  assert.match(js, /window\.fuaLevel\.mean\(\)\.toFixed\(4\) \+ ' RMS'\); window\.fuaLevel\.reset\(\);/);
  assert.doesNotMatch(js, /fuaCommand\("frames"\)/);
});

// [rc4l] A 32x16 frame as the page hands it back, base64 PNG, one grey level with its left half at left.
function frame(left, right = 10) {
  const rgb = Buffer.alloc(32 * 16 * 3);
  for (let y = 0; y < 16; y++) for (let x = 0; x < 32; x++) rgb.fill(x < 16 ? left : right, (y * 32 + x) * 3, (y * 32 + x) * 3 + 3);
  return encodePNG(32, 16, rgb).toString('base64');
}

// [rc4l] Queue #68 took a scratch harness of its own to frame a cvar at two values in a browser, for want of this step.
test('an ab step frames a cvar at two values, sets it back, and reports how far apart the frames sit', () => {
  const steps = parseRun('god; ab fua_dg_clusters 1 0');
  assert.deepEqual(steps[1], { ab: { cvar: 'fua_dg_clusters', a: '1', b: '0' } });
  const js = runScript(steps);
  assert.match(js, /const grabFrame = /);
  assert.match(js, /Module\.fuaCommand\("fua_dg_clusters 1"\);/);
  assert.match(js, /Module\.fuaCommand\("fua_dg_clusters 0"\);/);
  assert.match(js, /abs\.push\(\{ cvar: "fua_dg_clusters", a: "1", b: "0", frames: \[first, second\] \}\);/);
  // [rc4l] At full scale, so a slow adapter's dynamic resolution cannot change the scale between the two frames, unless the scale is what is compared.
  assert.match(js, /fua_dg_dynres_force 1/);
  assert.doesNotMatch(runScript(parseRun('ab fua_dg_dynres_force 1 0.5')), /fuaCommand\('fua_dg_dynres_force 1'\);/);
  const ab = { cvar: 'fua_dg_clusters', a: '1', b: '0', frames: [frame(10), frame(90)] };
  assert.equal(abLine(ab), 'ab fua_dg_clusters 1 vs 0: mean|d| 40.000, largest block 80.00');
  assert.deepEqual(report({ said: [], sounds: [], heldBefore: 0, heldAfter: 0, abs: [ab] }, [])[0], abLine(ab));
  assert.deepEqual(abShots([ab]).map((s) => s.name), ['ab1-fua_dg_clusters-1.png', 'ab1-fua_dg_clusters-0.png']);
  assert.deepEqual(abShots([ab])[1].bytes, Buffer.from(ab.frames[1], 'base64'));
  assert.deepEqual(abShots(undefined), []);
});

test('a WGSL swap splits at its first arrow, and one without an arrow or a find is refused', () => {
  assert.deepEqual(parseSwap('(1.0f / gl_FragCoord.w)=>vPixelPos.w'), { find: '(1.0f / gl_FragCoord.w)', replace: 'vPixelPos.w' });
  assert.deepEqual(parseSwap('a=>b=>c'), { find: 'a', replace: 'b=>c' });
  assert.throws(() => parseSwap('nothing'), /--wgsl takes FIND=>REPLACE, not "nothing"/);
  assert.throws(() => parseSwap('=>x'), /--wgsl takes FIND=>REPLACE/);
});

test('the page starts with the watches, each WAD served and the swap, and only what was asked for', () => {
  const plain = initFor();
  assert.match(plain, /window\.fuaSounds = \[\]/);
  assert.doesNotMatch(plain, /createShaderModule|files\.json/);
  const all = initFor({ wads: [{ name: 'q.wad', bytes: Buffer.from('PWAD') }], swap: { find: 'a', replace: 'b' } });
  assert.match(all, /const name = "q\.wad";/);
  assert.match(all, /createShaderModule/);
});

test('a save given to a run is written into the page home and loaded before its steps', () => {
  const save = { name: 'fight.zds', bytes: Buffer.from('ZDS') };
  assert.match(loadSave(save), /FuaHome\.HOME \+ '\/' \+ "fight\.zds"/);
  assert.match(loadSave(save), /atob\("WkRT"\)/);
  const js = runScript(parseRun('wait 50'), { save });
  assert.ok(js.indexOf("fuaCommand('load ' + at)") > 0);
  assert.ok(js.indexOf("fuaCommand('load ' + at)") < js.indexOf('setTimeout(r, 50)'));
  assert.doesNotMatch(runScript(parseRun('wait 50')), /FuaHome\.HOME/);
});

test('stallLine names a page that stopped drawing for longer than the limit', () => {
  assert.equal(stallLine(undefined, 10000), '');
  assert.equal(stallLine({ ms: 9000, at: 0 }, 10000), '');
  assert.equal(stallLine({ ms: 41200, at: 13000 }, 10000), 'STALLED: the page drew nothing for 41.2 s from 13.0 s into the run, so what the steps after that printed is stale');
});

test('runScript reports the longest wait between frames over the run', () => {
  assert.match(runScript(parseRun('wait 100')), /stall: longestGap\(window\.fuaFrames\.slice\(runFramesFrom\)\)/);
});
