import { test } from 'node:test';
import assert from 'node:assert/strict';
import { CHECKS, blankGame, inPage, judge, savePicture, soundStarts } from '../check.mjs';

test('page code waits for the engine to start before it runs', () => {
  const js = inPage('return 1;');
  assert.match(js, /^\(async \(\) => \{ while \(!\(window\.Module && Module\.fuaCommand\)\)/);
  assert.match(js, /return 1; \}\)\(\)$/);
});

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

test('a check that wants a screenshot fails without one or with a blank one', () => {
  const draws = CHECKS[0];
  assert.ok(draws.shot);
  assert.deepEqual(judge(draws, '[eval] "drawn"', image((x) => x * 20)), { ok: true });
  assert.deepEqual(judge(draws, '[eval] "drawn"', null), { ok: false, reason: 'no screenshot' });
  assert.equal(judge(draws, '[eval] "drawn"', image(() => 0)).ok, false);
});

test('the sound check wants a context that waited for the click and ran after it', () => {
  assert.equal(soundStarts({ before: ['suspended'], after: ['running'], played: 12 }), '');
  assert.equal(soundStarts({ before: ['suspended'], after: ['running'], played: 0 }), 'nothing played in the two seconds after the click');
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

test('the checks run in the order a profile carries state through', () => {
  const names = CHECKS.map((c) => c.name);
  assert.ok(names.indexOf('quitting stops the loop and offers to play again') < names.indexOf('a reload keeps what was set before quitting'));
  assert.ok(names.indexOf('leaving the page stores the config') < names.indexOf('a reload keeps what was set before leaving'));
});
