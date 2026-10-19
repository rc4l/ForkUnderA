import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const FuaHome = createRequire(import.meta.url)('../../../web/home.js');

// [rc4l] A file system whose syncs answer at once, failing with fail when it is set, and record which way they went.
function fakeFS(fail = null) {
  const fs = { syncs: [], made: [], mounted: [] };
  fs.syncfs = (populate, done) => { fs.syncs.push(populate); done(fail); };
  fs.mkdirTree = (dir) => fs.made.push(dir);
  fs.mount = (type, opts, dir) => fs.mounted.push([type, dir]);
  return fs;
}

// [rc4l] A page to keep a home in: its document and window, the interval the keeper sets, and what it logs.
function fakePage() {
  const doc = new EventTarget();
  doc.visibilityState = 'visible';
  const page = { doc, win: new EventTarget(), logged: [], tick: null };
  page.every = (fn, ms) => { page.tick = fn; page.ms = ms; };
  page.log = (t) => page.logged.push(t);
  return page;
}

test('the home is mounted on IndexedDB and read in, a failure logged', async () => {
  const fs = fakeFS();
  assert.equal(await FuaHome.mount(fs, 'IDBFS', () => {}), true);
  assert.deepEqual(fs.made, [FuaHome.HOME]);
  assert.deepEqual(fs.mounted, [['IDBFS', FuaHome.HOME]]);
  assert.deepEqual(fs.syncs, [true]);
  const logged = [];
  assert.equal(await FuaHome.mount(fakeFS('quota'), 'IDBFS', (t) => logged.push(t)), false);
  assert.deepEqual(logged, ['Saved data could not be read: quota']);
});

test('the keeper stores every few seconds, and with the config when the page is hidden or left', async () => {
  const fs = fakeFS(), page = fakePage();
  let configs = 0, seen = null;
  const save = FuaHome.keeper({ FS: fs, log: page.log, writeConfig: () => configs++, onSaving: (p) => { seen = p; }, doc: page.doc, win: page.win, every: page.every });
  assert.equal(page.ms, FuaHome.EVERY_MS);
  page.tick();
  assert.equal(await seen, true);
  assert.equal(configs, 0);
  page.doc.dispatchEvent(new Event('visibilitychange'));
  assert.equal(configs, 0);
  page.doc.visibilityState = 'hidden';
  page.doc.dispatchEvent(new Event('visibilitychange'));
  page.win.dispatchEvent(new Event('pagehide'));
  assert.equal(configs, 2);
  assert.equal(await save(false), true);
  assert.deepEqual(fs.syncs, [false, false, false, false]);
});

test('a store waits for the one before it, and the timer skips while one waits', async () => {
  const fs = fakeFS(), page = fakePage();
  const save = FuaHome.keeper({ FS: fs, log: page.log, writeConfig: () => {}, doc: page.doc, win: page.win, every: page.every });
  const first = save(false);
  page.tick();
  assert.deepEqual(fs.syncs, []);
  await first;
  assert.deepEqual(fs.syncs, [false]);
});

test('a store that fails is logged and answers false', async () => {
  const page = fakePage();
  const save = FuaHome.keeper({ FS: fakeFS('full'), log: page.log, writeConfig: () => {}, doc: page.doc, win: page.win, every: page.every });
  assert.equal(await save(false), false);
  assert.deepEqual(page.logged, ['Saving failed: full']);
});

// [rc4l] A tree of folders (objects) and files (strings) as the engine's file system shows it.
function treeFS(tree) {
  const at = (path) => path.split('/').filter(Boolean).reduce((node, part) => node[part], tree);
  return {
    readdir: (dir) => ['.', '..', ...Object.keys(at(dir))],
    stat: (path) => ({ mode: typeof at(path) === 'object' ? 1 : 0 }),
    isDir: (mode) => mode === 1,
  };
}

test('a file is found by name anywhere under the home, or not at all', () => {
  const fs = treeFS({ home: { web_user: { '.config': { forkundera: { 'checkpicture.zds': 'x', 'forkundera.ini': 'y' } }, 'notes.txt': 'z' } } });
  assert.equal(FuaHome.find(fs, 'checkpicture.zds'), FuaHome.HOME + '/.config/forkundera/checkpicture.zds');
  assert.equal(FuaHome.find(fs, 'notes.txt'), FuaHome.HOME + '/notes.txt');
  assert.equal(FuaHome.find(fs, 'missing.zds'), null);
  assert.equal(FuaHome.find(fs, 'forkundera.ini', FuaHome.HOME + '/.config'), FuaHome.HOME + '/.config/forkundera/forkundera.ini');
});
