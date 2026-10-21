import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const { touchWanted, applyPointer, viewOf, insetPx } = createRequire(import.meta.url)('../../../web/touchlogic.js');

test('the controls are wanted on a coarse pointer with touch points, forced either way by the query, and hidden by a keyboard', () => {
  assert.equal(touchWanted({ coarse: true, touchPoints: 5, query: '', sawKeyboard: false }), true);
  assert.equal(touchWanted({ coarse: false, touchPoints: 0, query: '?map=MAP01', sawKeyboard: false }), false);
  assert.equal(touchWanted({ coarse: true, touchPoints: 0, query: '', sawKeyboard: false }), false, 'coarse without touch points is a TV remote');
  assert.equal(touchWanted({ coarse: true, touchPoints: 5, query: '', sawKeyboard: true }), false);
  assert.equal(touchWanted({ coarse: false, touchPoints: 0, query: '?touch=1', sawKeyboard: true }), true);
  assert.equal(touchWanted({ coarse: true, touchPoints: 5, query: '?map=MAP01&touch=0', sawKeyboard: false }), false);
  assert.equal(touchWanted({ coarse: true, touchPoints: 5, query: '?touch=12', sawKeyboard: false }), true, 'a malformed value is ignored');
});

// [rc4l] A tap between two engine reads must still reach it, so an ended pointer stays listed until read.
test('pointer events fold into the list the engine reads, ended ones kept', () => {
  let list = [];
  list = applyPointer(list, { type: 'down', id: 1, x: 10, y: 20 });
  list = applyPointer(list, { type: 'down', id: 2, x: 30, y: 40 });
  list = applyPointer(list, { type: 'move', id: 1, x: 15, y: 25 });
  assert.deepEqual(list, [{ id: 2, x: 30, y: 40, ox: 30, oy: 40, ended: false }, { id: 1, x: 15, y: 25, ox: 10, oy: 20, ended: false }], 'each keeps where it landed');
  list = applyPointer(list, { type: 'up', id: 1, x: 99, y: 99 });
  assert.deepEqual(list.find((p) => p.id === 1), { id: 1, x: 15, y: 25, ox: 10, oy: 20, ended: true }, 'ended where it last was');
  assert.deepEqual(applyPointer(list, { type: 'move', id: 1, x: 1, y: 1 }), list, 'an ended pointer no longer moves');
  assert.deepEqual(applyPointer(list, { type: 'move', id: 7, x: 1, y: 1 }), list, 'an unknown pointer does not appear from a move');
  assert.deepEqual(applyPointer(list, { type: 'up', id: 7, x: 1, y: 1 }), list);
  assert.deepEqual(applyPointer(list, { type: 'hover', id: 2, x: 1, y: 1 }), list);
  const revived = applyPointer(list, { type: 'down', id: 1, x: 5, y: 5 });
  assert.deepEqual(revived.find((p) => p.id === 1), { id: 1, x: 5, y: 5, ox: 5, oy: 5, ended: false });
  assert.deepEqual(applyPointer(list, { type: 'cancel', id: 2, x: 0, y: 0 }).find((p) => p.id === 2), { id: 2, x: 30, y: 40, ox: 30, oy: 40, ended: true });
});

test('the view is the canvas size, four insets and whether it is portrait', () => {
  assert.deepEqual(viewOf(390.4, 743.6, [0, 47, 0, 34]), { width: 390, height: 744, insets: [0, 47, 0, 34], portrait: true });
  assert.deepEqual(viewOf(844, 390, undefined), { width: 844, height: 390, insets: [0, 0, 0, 0], portrait: false });
  assert.deepEqual(viewOf(undefined, undefined, [NaN, 1]), { width: 0, height: 0, insets: [0, 1, 0, 0], portrait: false });
  assert.equal(insetPx('12px'), 12);
  assert.equal(insetPx(''), 0);
  assert.equal(insetPx(undefined), 0);
});
