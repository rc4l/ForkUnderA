import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const FuaData = createRequire(import.meta.url)('../../../web/data.js');

// [rc4l] A server with files.json and each file, sent in the pieces given, or failing with status for a name it lacks.
function fakeFetch(files) {
  return async (url) => {
    if (url === 'data/files.json') return { json: async () => Object.entries(files).map(([name, pieces]) => ({ name, size: pieces.flat().length })) };
    const pieces = files[url.slice('data/'.length)];
    if (!pieces) return { ok: false, status: 404 };
    let i = 0;
    return { ok: true, body: { getReader: () => ({ read: async () => (i < pieces.length ? { done: false, value: Uint8Array.from(pieces[i++]) } : { done: true }) }) } };
  };
}

test('every listed file is written at the root, with the whole download followed', async () => {
  const written = {}, progress = [];
  const FS = { writeFile: (p, bytes) => { written[p] = [...bytes]; } };
  await FuaData.fetchAll({ FS, fetch: fakeFetch({ 'a.pk3': [[1, 2], [3]], 'b.wad': [[4]] }), onProgress: (n, f) => progress.push([n, f]) });
  assert.deepEqual(written, { '/a.pk3': [1, 2, 3], '/b.wad': [4] });
  assert.deepEqual(progress, [['a.pk3', 0.5], ['a.pk3', 0.75], ['b.wad', 1]]);
});

test('a file the server lacks stops the load and names it', async () => {
  const fetch = fakeFetch({ 'a.pk3': [[1]] });
  const missing = async (url) => (url === 'data/files.json' ? { json: async () => [{ name: 'gone.wad', size: 1 }] } : fetch(url));
  await assert.rejects(FuaData.fetchAll({ FS: { writeFile() {} }, fetch: missing }), /Could not fetch gone.wad \(404\)/);
});
