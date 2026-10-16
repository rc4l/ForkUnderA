// [rc4l] The files the engine starts with (its pk3s and Freedoom), fetched from the page's data folder and written at the file system's root, where it looks first.
'use strict';

(function (root) {
  // [rc4l] Every file data/files.json lists, read in pieces so onProgress(name, fraction) can follow the whole download.
  async function fetchAll({ FS, fetch = (url) => root.fetch(url), onProgress = () => {} }) {
    const list = await (await fetch('data/files.json')).json();
    const total = list.reduce((n, f) => n + f.size, 0) || 1;
    let done = 0;
    for (const f of list) {
      const res = await fetch('data/' + f.name);
      if (!res.ok) throw new Error('Could not fetch ' + f.name + ' (' + res.status + ')');
      const reader = res.body.getReader();
      const bytes = new Uint8Array(f.size);
      let at = 0;
      for (;;) {
        const { done: end, value } = await reader.read();
        if (end) break;
        bytes.set(value, at);
        at += value.length;
        done += value.length;
        onProgress(f.name, done / total);
      }
      FS.writeFile('/' + f.name, bytes.subarray(0, at));
    }
  }

  const api = { fetchAll };
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.FuaData = api;
})(typeof window !== 'undefined' ? window : globalThis);
