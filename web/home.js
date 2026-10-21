// [rc4l] The player's home in the page: the engine's $HOME, its config and saves, kept in IndexedDB between visits.
'use strict';

(function (root) {
  // [rc4l] Where the engine keeps its config and saves (posix's $HOME).
  const HOME = '/home/web_user';
  // [rc4l] How often the saves go back to IndexedDB.
  const EVERY_MS = 5000;

  // [rc4l] One sync between the file system and IndexedDB, reading it in (populate) or storing to it, as a promise of whether it worked; a failure is logged after what.
  function sync(FS, populate, log, what) {
    return new Promise((resolve) => FS.syncfs(populate, (err) => { if (err) log(what + ': ' + err); resolve(!err); }));
  }

  // [rc4l] The home mounted on IndexedDB, with what is stored there read in.
  function mount(FS, IDBFS, log) {
    FS.mkdirTree(HOME);
    FS.mount(IDBFS, {}, HOME);
    return sync(FS, true, log, 'Saved data could not be read');
  }

  // [rc4l] The keeper, as save(withConfig): one store after another, the config written first (writeConfig) when asked; one every EVERY_MS unless one is waiting, and one with the config when the page is hidden or left; onSaving hears each store's promise.
  function keeper({ FS, log, writeConfig, onSaving = () => {}, doc = root.document, win = root, every = setInterval }) {
    let last = Promise.resolve(true), queued = 0;
    const save = (withConfig) => {
      if (withConfig) writeConfig();
      queued++;
      last = last.then(() => sync(FS, false, log, 'Saving failed')).finally(() => queued--);
      onSaving(last);
      return last;
    };
    every(() => { if (!queued) save(false); }, EVERY_MS);
    doc.addEventListener('visibilitychange', () => { if (doc.visibilityState === 'hidden') save(true); });
    win.addEventListener('pagehide', () => save(true));
    return save;
  }

  // [rc4l] The first file called name under dir (the home when not given), or null: where the engine put a save, found without the page naming the engine's save directory.
  function find(FS, name, dir = HOME) {
    for (const entry of FS.readdir(dir)) {
      if (entry === '.' || entry === '..') continue;
      const path = dir + '/' + entry;
      const found = FS.isDir(FS.stat(path).mode) ? find(FS, name, path) : entry === name ? path : null;
      if (found) return found;
    }
    return null;
  }

  const api = { HOME, EVERY_MS, mount, keeper, find };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.FuaHome = api;
})(typeof window !== 'undefined' ? window : globalThis);
