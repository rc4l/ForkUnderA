import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const FuaPointer = createRequire(import.meta.url)('../../../web/pointer.js');

// [rc4l] Headless Chrome threw WrongDocumentError from the request a key press let run, and the page took it for a crash and stopped the game.
test('a pointer lock thrown away is logged and the request returns', () => {
  const said = [];
  const canvas = { requestPointerLock() { throw Object.assign(new Error('The root document of this element is not valid for pointer lock.'), { name: 'WrongDocumentError' }); } };
  assert.equal(FuaPointer.guard(canvas, (l) => said.push(l)), true);
  assert.equal(canvas.requestPointerLock(), undefined);
  assert.deepEqual(said, ['Pointer lock refused: The root document of this element is not valid for pointer lock.']);
});

// [rc4l] Chrome rejects a request made right after the player left the lock, which reached the page as an unhandled rejection.
test('a pointer lock rejected is logged and the promise settles', async () => {
  const said = [];
  const canvas = { requestPointerLock: () => Promise.reject(new Error('The user has exited the lock before this request was completed.')) };
  FuaPointer.guard(canvas, (l) => said.push(l));
  await canvas.requestPointerLock();
  assert.deepEqual(said, ['Pointer lock refused: The user has exited the lock before this request was completed.']);
});

test('a pointer lock granted passes through untouched, with its element and arguments', async () => {
  const seen = [];
  const canvas = { id: 'canvas', requestPointerLock(options) { seen.push([this.id, options]); return Promise.resolve('locked'); } };
  const said = [];
  FuaPointer.guard(canvas, (l) => said.push(l));
  assert.equal(await canvas.requestPointerLock({ unadjustedMovement: true }), 'locked');
  assert.deepEqual(seen, [['canvas', { unadjustedMovement: true }]]);
  const old = { requestPointerLock() { seen.push('old'); } };
  FuaPointer.guard(old, (l) => said.push(l));
  assert.equal(old.requestPointerLock(), undefined);
  assert.equal(FuaPointer.guard({}, () => {}), false, 'nothing to guard');
  assert.deepEqual(said, []);
});

// [rc4l] Fullscreen grew the canvas from 680 to 749 rows with no resize event, and SDL kept scaling the mouse for the old size, vertical 9% slower than horizontal.
test('every change of the canvas size reaches SDL as a window resize', () => {
  const sent = [];
  class Event { constructor(type) { this.type = type; } }
  const win = { Event, dispatchEvent: (e) => sent.push(e.type) };
  let callback = null;
  const watched = [];
  class Observer { constructor(cb) { callback = cb; } observe(el) { watched.push(el); } }
  const canvas = { id: 'canvas' };
  assert.ok(FuaPointer.followSize(canvas, win, Observer));
  assert.deepEqual(watched, [canvas]);
  callback([{ target: canvas }]);
  callback([{ target: canvas }]);
  assert.deepEqual(sent, ['resize', 'resize']);
  assert.equal(FuaPointer.followSize(canvas, win, undefined), null, 'no observer to watch with');
});
