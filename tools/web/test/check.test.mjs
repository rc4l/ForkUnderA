import { test } from 'node:test';
import assert from 'node:assert/strict';
import { CHECKS, LIT_FLOOR, blankGame, checkUrl, configuredOnce, crashOffered, crosshairCentred, destroyedAsReleased, everyFormatPlayed, gpuError, gridMatches, heardAtDistance, heldOnce, judge, marked, notIn, playedOnce, reportSent, saveLoads, savePicture, selectChecks, soakHeld, soundStarts, stoppedOffered, threadedWithSound } from '../check.mjs';
import { encodePNG } from '../../fuactl/src/pngcore.mjs';
import { BOXES as POINTLIGHT_BOXES } from '../../fuactl/scenarios/pointlight.mjs';

test('a check fails when its line never came', () => {
  assert.deepEqual(judge({ until: 'x' }, null), { ok: false, reason: 'no line matched /x/' });
});

test('a check fails with what its evaluation threw', () => {
  assert.deepEqual(judge({ until: 'x', expect: () => '' }, '[eval] threw Exception'), { ok: false, reason: 'threw Exception' });
});

test('a check without an expectation passes on its line', () => {
  assert.deepEqual(judge({ until: 'x' }, '[log] x'), { ok: true });
});

test('an expectation reads the evaluation and names what is wrong', () => {
  const quit = CHECKS.find((c) => c.name.startsWith('quitting'));
  assert.deepEqual(judge(quit, '[eval] {"again":true,"frames":0}'), { ok: true });
  assert.deepEqual(judge(quit, '[eval] {"again":false,"frames":0}'), { ok: false, reason: 'the page did not offer to play again' });
  assert.deepEqual(judge(quit, '[eval] {"again":true,"frames":3}'), { ok: false, reason: '3 frames ran after quitting' });
});

test('an error check tells a fatal error from a recoverable one by the page and the frames', () => {
  const named = (n) => CHECKS.find((c) => c.name.startsWith(n));
  const recoverable = named('a recoverable error');
  assert.deepEqual(judge(recoverable, '[eval] {"frames":40,"shown":false}'), { ok: true });
  assert.deepEqual(judge(recoverable, '[eval] {"frames":40,"shown":true}'), { ok: false, reason: 'the page showed it as fatal' });
  assert.deepEqual(judge(recoverable, '[eval] {"frames":0,"shown":false}'), { ok: false, reason: 'no frames ran after it' });
  const fatal = named('a fatal error');
  assert.deepEqual(judge(fatal, '[eval] {"frames":0,"shown":"checking"}'), { ok: true });
  assert.deepEqual(judge(fatal, '[eval] {"frames":0,"shown":""}'), { ok: false, reason: 'the page did not show it' });
  assert.deepEqual(judge(fatal, '[eval] {"frames":2,"shown":"checking"}'), { ok: false, reason: '2 frames ran after it' });
});

// [rc4l] A 10x10 RGB image whose pixel at x, y is fill(x, y).
function image(fill) {
  const data = Buffer.alloc(10 * 10 * 3);
  for (let y = 0; y < 10; y++) for (let x = 0; x < 10; x++) data.fill(fill(x, y), (y * 10 + x) * 3, (y * 10 + x) * 3 + 3);
  return { w: 10, h: 10, channels: 3, colorType: 2, data };
}

test('a screenshot shows the game unless it is black or one flat colour', () => {
  assert.equal(blankGame(image((x) => x * 20)), '');
  assert.match(blankGame(image(() => 0)), /^the game is black/);
  assert.match(blankGame(image(() => 128)), /^the game is one flat colour/);
});

test('a resized frame keeps its crosshair in the middle', () => {
  const at = (cx) => image((x) => (x === cx ? 0 : 60));
  const green = (cx) => { const img = at(cx); for (let y = 0; y < 10; y++) img.data.set([0, 255, 0], (y * 10 + cx) * 3); return img; };
  assert.equal(crosshairCentred(green(5)), '');
  assert.equal(crosshairCentred(green(8)), 'the crosshair is at 80% across, not the middle');
  assert.equal(crosshairCentred(image(() => 60)), 'no crosshair in the frame');
});

test('a check that wants a screenshot fails without one or with a blank one', () => {
  const draws = CHECKS[0];
  assert.ok(draws.download);
  assert.deepEqual(judge(draws, '[eval] "drawn"', image((x) => x * 20)), { ok: true });
  assert.deepEqual(judge(draws, '[eval] "drawn"', null), { ok: false, reason: 'no screenshot' });
  assert.equal(judge(draws, '[eval] "drawn"', image(() => 0)).ok, false);
});

test('the sound check wants a context that waited for the click and ran after it', () => {
  assert.equal(soundStarts({ before: ['suspended'], after: ['running'], played: 12 }), '');
  assert.equal(soundStarts({ before: ['suspended'], after: ['running'], played: 0 }), 'nothing played in the six seconds after the click');
  assert.equal(soundStarts({ before: [], after: [] }), 'the engine made no audio context');
  assert.equal(soundStarts({ before: ['running'], after: ['running'] }), 'the sound did not wait for a click (running)');
  assert.equal(soundStarts({ before: ['suspended'], after: ['suspended'] }), 'the sound stayed suspended after a click');
});

test('a save needs its own picture, not the placeholder', () => {
  assert.equal(savePicture({ found: true, width: 216, height: 162, range: 120 }), '');
  assert.equal(savePicture({ found: true, width: 216, height: 162, range: 0 }), "the save's picture is one flat colour (range 0)");
  assert.equal(savePicture({ found: false }), 'the save was not written');
  assert.equal(savePicture({ found: true, width: 1, height: 1 }), 'the save kept its placeholder picture (1x1)');
});

test('a save comes back when the file is there and the engine takes it', () => {
  assert.equal(saveLoads({ found: true, said: 'map01 - entryway\n' }), '');
  assert.equal(saveLoads({ found: false }), 'the save was gone after the reload');
  assert.equal(saveLoads({ found: true, said: 'Could not read savegame x\n' }), 'loading it said: Could not read savegame x');
});

test('--only picks the checks whose names contain its text', () => {
  assert.equal(selectChecks(CHECKS, null), CHECKS);
  assert.deepEqual(selectChecks(CHECKS, 'save').map((c) => c.name), ['a save gets its picture', 'a save survives a reload and loads']);
});

test('the checks run in the order a profile carries state through', () => {
  const names = CHECKS.map((c) => c.name);
  assert.ok(names.indexOf('quitting stops the loop and offers to play again') < names.indexOf('a reload keeps what was set before quitting'));
  assert.ok(names.indexOf('leaving the page stores the config') < names.indexOf('a reload keeps what was set before leaving'));
  assert.ok(names.indexOf('a save gets its picture') < names.indexOf('a save survives a reload and loads'));
});

test('any device error fails a check, named by its first line', () => {
  const lines = ['[log] map01', '[log] WebGPU error: Validation Error\n\nCaused by:\n  In Device::create_bind_group', '[log] WebGPU error: later'];
  assert.equal(gpuError(lines), 'the device reported: Validation Error Caused by: In Device::create_bind_group');
  assert.equal(gpuError(['[log] map01']), '');
  assert.deepEqual(judge({ until: 'x' }, '[log] x', null, lines), { ok: false, reason: gpuError(lines) });
  assert.deepEqual(judge({ until: 'x' }, '[log] x', null, ['[log] map01']), { ok: true });
});

test('a check that makes a device error on purpose is not failed by it', () => {
  const lines = ['[log] WebGPU error: checking out of memory'];
  assert.deepEqual(judge({ until: 'x', deviceError: true }, '[log] x', null, lines), { ok: true });
});

test('a game that can no longer draw has to show it, offer its report and stop the loop', () => {
  const good = { frames: 0, shown: 'The game crashed: out of graphics memory (checking out of memory)', offered: true };
  assert.equal(stoppedOffered(good), '');
  assert.equal(stoppedOffered({ ...good, shown: '' }), 'the page showed ""');
  assert.equal(stoppedOffered({ ...good, offered: false }), 'the page offered no report');
  assert.equal(stoppedOffered({ ...good, frames: 3 }), '3 frames ran after it');
});

// [rc4l] Twenty sizes held twenty sets of six screen textures before the backend destroyed what it let go of.
test('resizing may hold one more set of screen targets, not one per size', () => {
  assert.equal(heldOnce({ before: 6, after: 6 }), '');
  assert.equal(heldOnce({ before: 6, after: 12 }), '');
  assert.equal(heldOnce({ before: 6, after: 126 }), '120 more screen-sized textures were held after the resizes (6 before)');
});

test('the page may hold what the engine holds by tag, give or take alignment, and no more', () => {
  const row = (name, last) => ({ name, last });
  assert.equal(destroyedAsReleased([row('gpu/page/fua scene VB', 18_700_000), row('gpu/mem/tag/fua scene VB', 18_700_000), row('gpu/page/Upload memory page', 1e8)]), '');
  assert.equal(destroyedAsReleased([row('gpu/page/fua material', 60_000)]), '');
  assert.equal(destroyedAsReleased([row('gpu/page/fua scene VB', 56_100_000), row('gpu/mem/tag/fua scene VB', 18_700_000)]),
    "the page held what the engine let go of: fua scene VB 54785 KB held against the engine's 18262 KB");
  assert.equal(destroyedAsReleased([]), 'no GPU readings');
});

test('a soak passes on the engine verdict and otherwise names what grew', () => {
  assert.equal(soakHeld({ pass: true, marks: 7, judged: 40, grew: [] }), '');
  const grew = [{ name: 'gpu/process_bytes', text: 'gpu/process_bytes grew 18.0 MB a cycle (90.0 MB over 5 cycles)' }, { name: 'mem/heap_bytes', text: 'mem/heap_bytes grew 1.0 MB a cycle (5.0 MB over 5 cycles)' }];
  assert.equal(soakHeld({ pass: false, marks: 7, judged: 40, grew }), 'kept growing with each repeat: gpu/process_bytes grew 18.0 MB a cycle (90.0 MB over 5 cycles); mem/heap_bytes grew 1.0 MB a cycle (5.0 MB over 5 cycles)');
  assert.match(soakHeld({ pass: false, marks: 7, judged: 40, grew, kept: ['fua material +1700 KB'] }), /; the page's WebGPU objects grew in: fua material \+1700 KB$/);
  assert.equal(soakHeld({ pass: false, marks: 2, judged: 0, grew: [] }), 'the soak judged nothing (2 marks, 0 counters)');
  assert.equal(soakHeld({}), 'the soak judged nothing (undefined marks, undefined counters)');
});

test('a sound has to start once, not again after it ends', () => {
  assert.equal(playedOnce(1), '');
  assert.equal(playedOnce(2), 'the sound started 2 times');
  assert.equal(playedOnce(0), 'the sound never started');
});

test('a sound of every format has to start', () => {
  assert.equal(everyFormatPlayed({ FLAC: 1, 'Ogg Vorbis': 2, WAV: 1 }), '');
  assert.equal(everyFormatPlayed({ FLAC: 0, 'Ogg Vorbis': 1, WAV: 0 }), 'nothing played in FLAC, WAV');
});

test('firing at the floor has to put a mark through the decal pass', () => {
  assert.equal(marked({ boxes: 3, draws: 1 }), '');
  assert.equal(marked({ boxes: 0, draws: 0 }), 'the decal pass drew no mark');
});

test('the canvas check fails on any zero size', () => {
  const zero = CHECKS.find((c) => c.name.startsWith('the canvas never'));
  assert.deepEqual(judge(zero, '[eval] 0'), { ok: true });
  assert.deepEqual(judge(zero, '[eval] 2'), { ok: false, reason: 'the canvas was sized to zero 2 times' });
});

test('a crash in a frame has to show, offer its report, stop the loop and reach the engine in its stack', () => {
  const good = { frames: 0, shown: 'checking a crash', offered: true, status: 'This build does not send crash reports; copy or save the report to share it.', stack: true };
  assert.equal(crashOffered(good), '');
  assert.equal(crashOffered({ ...good, shown: '' }), 'the page did not show the crash');
  assert.equal(crashOffered({ ...good, offered: false }), 'the page offered no report');
  assert.equal(crashOffered({ ...good, frames: 4 }), '4 frames ran after the crash');
  assert.equal(crashOffered({ ...good, stack: false }), "the report's stack has no frame in the engine");
  assert.equal(crashOffered({ ...good, status: 'The report was sent to the developers.' }), 'the page said: The report was sent to the developers.');
});

test('a sent report needs the engine to have told the page its game and setting, and the tracker its address', () => {
  const good = { release: 'ForkUnderA@v1', map: 'map01', files: ['fua_core.pk3', 'freedoom2.wad'], off: false, on: true, url: 'https://127.0.0.1/api/1/envelope/?sentry_key=checking&sentry_version=7', event: { level: 'fatal', files: 'fua_core.pk3, freedoom2.wad' }, status: 'The report was sent to the developers.' };
  assert.equal(reportSent(good), '');
  assert.equal(reportSent({ ...good, release: '' }), 'the engine told the page release "" and map "map01"');
  assert.equal(reportSent({ ...good, map: '' }), 'the engine told the page release "ForkUnderA@v1" and map ""');
  assert.equal(reportSent({ ...good, files: ['fua_core.pk3'] }), 'the engine told the page the files fua_core.pk3');
  assert.equal(reportSent({ ...good, off: true }), 'cl_crashreports did not reach the page (off true, on true)');
  assert.equal(reportSent({ ...good, on: false }), 'cl_crashreports did not reach the page (off false, on false)');
  assert.equal(reportSent({ ...good, url: '' }), 'no report was sent');
  assert.equal(reportSent({ ...good, url: 'https://elsewhere/' }), 'the report went to https://elsewhere/');
  assert.equal(reportSent({ ...good, event: null }), 'the report sent did not carry the game');
  assert.equal(reportSent({ ...good, event: { level: 'error', files: 'freedoom2.wad' } }), 'the report sent did not carry the game');
  assert.equal(reportSent({ ...good, event: { level: 'fatal', files: '' } }), 'the report sent did not carry the game');
  assert.equal(reportSent({ ...good, status: 'The report could not be sent' }), 'the page said: The report could not be sent');
});

test('the canvas is configured exactly once without a resize', () => {
  assert.equal(configuredOnce(1), '');
  assert.equal(configuredOnce(2), 'the canvas was configured 2 times');
  assert.equal(configuredOnce(0), 'the canvas was configured 0 times');
});

test('the game thread check needs tics on the thread and a sound from them', () => {
  assert.equal(threadedWithSound({ tics: 40, sounds: 3 }), '');
  assert.equal(threadedWithSound({ tics: 0, sounds: 3 }), 'no tic ran on the game thread');
  assert.equal(threadedWithSound({ tics: 40, sounds: 0 }), 'the shots fired on the game thread made no sound');
});

// [rc4l] A CI runner's browser rendered silence for every sound, so distance is judged only where a sound with no distance was heard.
test('distant shooters are judged only where a sound with no distance was heard', () => {
  assert.equal(heardAtDistance({ control: 0.3, peak: 0.2, contexts: [] }), '');
  assert.equal(heardAtDistance({ control: 0.0928, peak: 0.0475, contexts: [] }), '');
  assert.match(heardAtDistance({ control: 0.0928, peak: 0.02, contexts: [] }), /came out at 0.02 RMS/);
  assert.match(heardAtDistance({ control: 0.3, peak: 0, contexts: [] }), /shooters across the room came out at 0 RMS at most/);
  assert.match(heardAtDistance({ control: 0, peak: 0, contexts: [{ state: 'running', time: 3 }] }).skip, /made no sound at all/);
  const check = { until: 'x', expect: heardAtDistance };
  assert.deepEqual(judge(check, '[eval] {"control":0,"peak":0,"contexts":[]}'), { ok: true, skip: 'the page made no sound at all, a sound with no distance peaking at 0 RMS (audio [])' });
});

// [rc4l] A 32x32 frame as the page hands it back, base64 PNG, dark but for a bright square of side lit at the top left.
function frame(lit, level = 200) {
  const rgb = Buffer.alloc(32 * 32 * 3, 10);
  for (let y = 0; y < lit; y++) for (let x = 0; x < lit; x++) rgb.fill(level, (y * 32 + x) * 3, (y * 32 + x) * 3 + 3);
  return encodePNG(32, 32, rgb).toString('base64');
}

test("the light grid has to draw what no grid draws, where the light showed at all, by the pointlight scenario's ceiling", () => {
  const lit = frame(16), dark = frame(0);
  assert.equal(gridMatches([{ name: 'd600', grid: lit, none: lit, dark }]), '');
  assert.equal(gridMatches([{ name: 'd600', grid: dark, none: lit, dark }]), `at d600 the light grid drew 44.531 away from no grid, over ${POINTLIGHT_BOXES.MAP01.frame.ceiling}`);
  const faint = frame(16, 10 + LIT_FLOOR - 1);
  assert.equal(gridMatches([{ name: 'd600', grid: faint, none: faint, dark }]), 'at d600 the light changed no block by more than 2.81, so it never drew');
});

test("the light grid check plays the pointlight scenario's hall from its vantages", () => {
  const check = CHECKS.find((c) => c.name.startsWith('a distant light'));
  assert.ok(check.firefox);
  assert.equal(check.query, 'file=fua-pointlight.wad&map=MAP01');
  assert.match(check.init, /"fua-pointlight\.wad"/);
  for (const name of ['d600', 'd1000', 'd1500', 'd2200', 'd3000']) assert.match(check.evaluate, new RegExp(`"name":"${name}"`));
});

test("a check with a query of its own opens the run's page with it in place of the run's", () => {
  assert.equal(checkUrl('http://127.0.0.1:8/?map=MAP01', {}), 'http://127.0.0.1:8/?map=MAP01');
  assert.equal(checkUrl('http://127.0.0.1:8/?map=MAP01', { query: 'file=a.wad&map=MAP02' }), 'http://127.0.0.1:8/?file=a.wad&map=MAP02');
});

test('Firefox runs only the checks marked for it, and Chrome runs every one', () => {
  assert.equal(notIn('chrome', {}), '');
  assert.equal(notIn('firefox', { firefox: true }), '');
  assert.equal(notIn('firefox', {}), 'it needs what only the Chrome driver does');
});
