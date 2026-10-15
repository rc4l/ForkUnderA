import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const FuaWads = createRequire(import.meta.url)('../../../web/wads.js');

test('an IWAD is known by its header or its extension', () => {
  assert.ok(FuaWads.isIwad('doom2.wad', Uint8Array.from([73, 87, 65, 68])));
  assert.ok(FuaWads.isIwad('game.ipk3', Uint8Array.from([80, 75, 3, 4])));
  assert.ok(!FuaWads.isIwad('map.wad', Uint8Array.from([80, 87, 65, 68])));
});

test('the chosen WADs that are stored become -iwad and -file, and the rest are left out', () => {
  const names = new Set(['doom2.wad', 'a.wad', 'b.pk3']);
  assert.deepEqual(FuaWads.argsFor({ iwad: 'doom2.wad', files: ['a.wad', 'gone.wad', 'b.pk3'] }, names, '/wads'),
    ['-iwad', '/wads/doom2.wad', '-file', '/wads/a.wad', '/wads/b.pk3']);
  assert.deepEqual(FuaWads.argsFor({ iwad: 'gone.wad', files: [] }, names, '/wads'), []);
});

test('picking an IWAD replaces the one to play, and a PWAD is loaded once or dropped', () => {
  const c = { iwad: 'doom.wad', files: ['a.wad'] };
  assert.deepEqual(FuaWads.picked(c, 'doom2.wad', true, true), { iwad: 'doom2.wad', files: ['a.wad'] });
  assert.deepEqual(FuaWads.picked(c, 'doom.wad', true, false), { iwad: null, files: ['a.wad'] });
  assert.deepEqual(FuaWads.picked(c, 'a.wad', false, true), c);
  assert.deepEqual(FuaWads.picked(c, 'b.wad', false, true), { iwad: 'doom.wad', files: ['a.wad', 'b.wad'] });
  assert.deepEqual(FuaWads.picked(c, 'a.wad', false, false), { iwad: 'doom.wad', files: [] });
});

test('a removed WAD leaves the choice', () => {
  assert.deepEqual(FuaWads.without({ iwad: 'doom2.wad', files: ['a.wad'] }, 'doom2.wad'), { iwad: null, files: ['a.wad'] });
  assert.deepEqual(FuaWads.without({ iwad: 'doom2.wad', files: ['a.wad'] }, 'a.wad'), { iwad: 'doom2.wad', files: [] });
});

// [rc4l] A wad_reload in the page reloads it, and the next load alone starts on the arguments the engine handed over.
test('a restart hands its arguments to the next load once', () => {
  const kept = new Map();
  const store = { setItem: (k, v) => kept.set(k, v), getItem: (k) => kept.get(k) ?? null, removeItem: (k) => kept.delete(k) };
  FuaWads.keepRestartArgs('["-iwad","/freedoom1.wad","+map","E1M1"]', store);
  assert.deepEqual(FuaWads.takeRestartArgs(store), ['-iwad', '/freedoom1.wad', '+map', 'E1M1']);
  assert.equal(FuaWads.takeRestartArgs(store), null);
  FuaWads.keepRestartArgs('{"not":"a list"}', store);
  assert.equal(FuaWads.takeRestartArgs(store), null);
  const broken = { setItem: () => { throw new Error('private window'); }, getItem: () => { throw new Error('private window'); } };
  FuaWads.keepRestartArgs('["-x"]', broken);
  assert.equal(FuaWads.takeRestartArgs(broken), null);
});
