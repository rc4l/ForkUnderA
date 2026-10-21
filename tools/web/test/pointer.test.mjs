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

// [rc4l] Safari's lock notice is a bar in the window chrome, 47 rows off the page; Chrome's and Firefox's float over it.
test('only Safari is read as a browser whose lock notice shifts the page', () => {
  const ua = (userAgent) => ({ userAgent });
  assert.equal(FuaPointer.noticeShiftsLayout(ua('Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/26.6 Safari/605.1.15')), true);
  assert.equal(FuaPointer.noticeShiftsLayout(ua('Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/155.0.0.0 Safari/537.36')), false);
  assert.equal(FuaPointer.noticeShiftsLayout(ua('Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:140.0) Gecko/20100101 Firefox/140.0')), false);
  assert.equal(FuaPointer.noticeShiftsLayout(ua('Mozilla/5.0 (Windows NT 10.0) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/155.0.0.0 Safari/537.36 Edg/155.0.0.0')), false);
  assert.equal(FuaPointer.noticeShiftsLayout(undefined), false);
});

test('where the notice shifts the page, a lock asked for in a window takes the canvas fullscreen in the same gesture and locks at once', async () => {
  const seen = [];
  const doc = { fullscreenElement: null, fullscreenEnabled: true };
  const canvas = {
    requestPointerLock() { seen.push('lock'); return Promise.resolve('locked'); },
    requestFullscreen() { seen.push('fullscreen'); return Promise.resolve(); },
  };
  assert.equal(FuaPointer.fullscreenFirst(canvas, doc, true), true);
  assert.equal(await canvas.requestPointerLock(), 'locked');
  assert.deepEqual(seen, ['fullscreen', 'lock']);
  doc.fullscreenElement = canvas;
  seen.length = 0;
  assert.equal(await canvas.requestPointerLock(), 'locked');
  assert.deepEqual(seen, ['lock'], 'already fullscreen: the lock alone');
});

test('a fullscreen refused, or one the browser cannot offer, still asks for the lock', async () => {
  const seen = [];
  const refusing = { requestPointerLock() { seen.push('lock'); return 'asked'; }, requestFullscreen() { seen.push('fullscreen'); return Promise.reject(new Error('no gesture')); } };
  FuaPointer.fullscreenFirst(refusing, { fullscreenElement: null }, true);
  assert.equal(await refusing.requestPointerLock(), 'asked');
  assert.deepEqual(seen, ['fullscreen', 'lock']);
  seen.length = 0;
  const old = { requestPointerLock() { seen.push('lock'); }, requestFullscreen() { seen.push('fullscreen'); } };
  FuaPointer.fullscreenFirst(old, { fullscreenElement: null }, true);
  old.requestPointerLock();
  assert.deepEqual(seen, ['fullscreen', 'lock'], 'a fullscreen request with no promise is followed by the lock as well');
  seen.length = 0;
  const disabled = { requestPointerLock() { seen.push('lock'); }, requestFullscreen() { seen.push('fullscreen'); } };
  FuaPointer.fullscreenFirst(disabled, { fullscreenElement: null, fullscreenEnabled: false }, true);
  disabled.requestPointerLock();
  assert.deepEqual(seen, ['lock'], 'fullscreen not allowed: the lock alone');
});

test('elsewhere, or without fullscreen to ask for, the lock is left as it is', () => {
  const plain = { requestPointerLock() { return 'plain'; }, requestFullscreen() { throw new Error('not asked'); } };
  assert.equal(FuaPointer.fullscreenFirst(plain, { fullscreenElement: null }, false), false);
  assert.equal(plain.requestPointerLock(), 'plain');
  assert.equal(FuaPointer.fullscreenFirst({ requestPointerLock() {} }, {}, true), false, 'no requestFullscreen');
  assert.equal(FuaPointer.fullscreenFirst({}, {}, true), false, 'nothing to wrap');
});
