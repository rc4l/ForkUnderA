// [rc4l] The player's own WADs: kept in IndexedDB between visits, listed in the page's panel, and written into the engine's file system with -iwad and -file for the ones chosen.
'use strict';

(function (root) {
  const DB = 'fua-wads', STORE = 'files', CHOICE = 'fua-wad-choice', DIR = '/wads';

  function open() {
    return new Promise((resolve, reject) => {
      const req = indexedDB.open(DB, 1);
      req.onupgradeneeded = () => req.result.createObjectStore(STORE, { keyPath: 'name' });
      req.onsuccess = () => resolve(req.result);
      req.onerror = () => reject(req.error);
    });
  }

  function run(mode, body) {
    return open().then((db) => new Promise((resolve, reject) => {
      const tx = db.transaction(STORE, mode);
      const result = body(tx.objectStore(STORE));
      tx.oncomplete = () => resolve(result && result.result !== undefined ? result.result : undefined);
      tx.onerror = () => reject(tx.error);
    }));
  }

  const all = () => run('readonly', (s) => s.getAll());
  const put = (file) => run('readwrite', (s) => s.put(file));
  const remove = (name) => run('readwrite', (s) => s.delete(name));

  // [rc4l] An IWAD by its header (IWAD) or its extension (.iwad, .ipk3), since only an IWAD can be the game.
  function isIwad(name, bytes) {
    const magic = String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]);
    return magic === 'IWAD' || /\.(iwad|ipk3)$/i.test(name);
  }

  // [rc4l] -iwad and -file for the chosen WADs among those stored (names), each under dir.
  function argsFor(c, names, dir) {
    const args = [];
    if (c.iwad && names.has(c.iwad)) args.push('-iwad', dir + '/' + c.iwad);
    const pwads = c.files.filter((n) => names.has(n));
    if (pwads.length) args.push('-file', ...pwads.map((n) => dir + '/' + n));
    return args;
  }

  // [rc4l] The choice with a WAD picked or not: an IWAD is the one to play or none, a PWAD is loaded or not.
  function picked(c, name, iwad, on) {
    if (iwad) return { ...c, iwad: on ? name : null };
    return { ...c, files: on ? [...new Set([...c.files, name])] : c.files.filter((n) => n !== name) };
  }

  // [rc4l] The choice without a WAD that was removed.
  function without(c, name) {
    return { iwad: c.iwad === name ? null : c.iwad, files: c.files.filter((n) => n !== name) };
  }

  function choice() {
    try { return JSON.parse(localStorage.getItem(CHOICE)) || { iwad: null, files: [] }; }
    catch { return { iwad: null, files: [] }; }
  }

  function setChoice(c) {
    try { localStorage.setItem(CHOICE, JSON.stringify(c)); } catch { /* private window: the choice lasts this visit */ }
  }

  // [rc4l] Every stored WAD into the file system, and the arguments for the chosen ones.
  async function load(FS) {
    const files = await all().catch(() => []);
    if (files.length === 0) return [];
    FS.mkdirTree(DIR);
    for (const f of files) FS.writeFile(DIR + '/' + f.name, new Uint8Array(f.data));
    return argsFor(choice(), new Set(files.map((f) => f.name)), DIR);
  }

  // [rc4l] The panel: the stored WADs with the IWAD to play and the PWADs to load, adding and removing files, and `restart` to apply.
  function panel(restart) {
    const dialog = document.getElementById('wads');
    const list = document.getElementById('wads-list');
    const input = document.getElementById('wads-add');

    async function render() {
      const files = await all().catch(() => []);
      const c = choice();
      list.replaceChildren();
      if (files.length === 0) {
        const p = document.createElement('p');
        p.textContent = 'No WADs yet. Freedoom plays until you add your own.';
        list.append(p);
        return;
      }
      for (const f of files) {
        const row = document.createElement('div');
        row.className = 'wad-row';
        const pick = document.createElement('input');
        pick.type = f.iwad ? 'radio' : 'checkbox';
        pick.name = f.iwad ? 'iwad' : '';
        pick.checked = f.iwad ? c.iwad === f.name : c.files.includes(f.name);
        pick.addEventListener('change', () => setChoice(picked(choice(), f.name, f.iwad, pick.checked)));
        const label = document.createElement('label');
        label.append(pick, ` ${f.name} `, Object.assign(document.createElement('small'), { textContent: f.iwad ? 'IWAD' : 'PWAD' }));
        const del = Object.assign(document.createElement('button'), { type: 'button', textContent: 'Remove' });
        del.addEventListener('click', async () => {
          await remove(f.name);
          setChoice(without(choice(), f.name));
          render();
        });
        row.append(label, del);
        list.append(row);
      }
    }

    input.addEventListener('change', async () => {
      for (const file of input.files) {
        const data = await file.arrayBuffer();
        await put({ name: file.name, data, iwad: isIwad(file.name, new Uint8Array(data, 0, Math.min(4, data.byteLength))) });
      }
      input.value = '';
      render();
    });
    document.getElementById('wads-button').addEventListener('click', () => { render(); dialog.showModal(); });
    document.getElementById('wads-close').addEventListener('click', () => dialog.close());
    document.getElementById('wads-apply').addEventListener('click', () => restart());
  }

  // [rc4l] The arguments a restart hands the next load (a wad_reload's new WAD set), kept for this tab only and taken once.
  const RESTART = 'fua-restart-args';
  function keepRestartArgs(json, store = root.sessionStorage) {
    try { store.setItem(RESTART, json); } catch { /* no session storage: the next load starts as a fresh one */ }
  }
  function takeRestartArgs(store = root.sessionStorage) {
    try {
      const json = store.getItem(RESTART);
      store.removeItem(RESTART);
      const args = JSON.parse(json);
      return Array.isArray(args) ? args : null;
    } catch { return null; }
  }

  const api = { load, panel, isIwad, argsFor, picked, without, keepRestartArgs, takeRestartArgs };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.FuaWads = api;
})(typeof window !== 'undefined' ? window : globalThis);
