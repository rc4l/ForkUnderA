import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const { stripAspect, computeLayout } = createRequire(import.meta.url)('../../../web/layout.js');

test('a strip is asked for by the query as width over height', () => {
  assert.equal(stripAspect('?strip=4:3'), 4 / 3);
  assert.equal(stripAspect('?map=MAP01&strip=16:9'), 16 / 9);
  assert.equal(stripAspect('?map=MAP01'), null);
  assert.equal(stripAspect('?strip=4:0'), null);
  assert.equal(stripAspect(undefined), null);
});

test('the game fills the view unless portrait with a strip asked for, when the band below takes the rest', () => {
  assert.deepEqual(computeLayout(844, 390, { aspect: 4 / 3 }), { portrait: false, mainHeight: null, band: 0 });
  assert.deepEqual(computeLayout(390, 844, {}), { portrait: true, mainHeight: null, band: 0 });
  assert.deepEqual(computeLayout(390, 844, { aspect: 4 / 3, footer: 44 }), { portrait: true, mainHeight: 293, band: 507 });
  // [rc4l] A strip taller than the view is the view.
  assert.deepEqual(computeLayout(390, 300, { aspect: 0.5 }), { portrait: false, mainHeight: null, band: 0 });
  assert.deepEqual(computeLayout(300, 390, { aspect: 0.5, footer: 400 }), { portrait: true, mainHeight: 0, band: 0 });
});
