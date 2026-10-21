import { test } from 'node:test';
import assert from 'node:assert/strict';
import { handle, isolated, DEMO_PATH } from '../demo/respond.mjs';
import worker from '../demo/worker.mjs';
import { ISOLATION_HEADERS } from '../headers.mjs';

// [rc4l] A stand-in for the Worker's ASSETS binding: the named paths answer with their bodies (304 to an If-None-Match of their ETag), anything else 404s, and every path asked for is kept.
function fakeAssets(files) {
  const asked = [];
  return {
    asked,
    fetch: async (req) => {
      const p = new URL(req.url).pathname;
      asked.push(p);
      if (!(p in files)) return new Response('nope', { status: 404 });
      if (req.headers.get('If-None-Match') === '"e1"') return new Response(null, { status: 304, headers: { ETag: '"e1"' } });
      return new Response(files[p], { headers: { 'Content-Type': 'application/gzip', ETag: '"e1"' } });
    },
  };
}

function assertIsolated(res) {
  for (const [k, v] of Object.entries(ISOLATION_HEADERS)) assert.equal(res.headers.get(k), v, k);
}

test('the bare site redirects to the page with its query, isolated', async () => {
  const res = await handle(new Request('https://fua.example/'), fakeAssets({}));
  assert.equal(res.status, 302);
  assert.equal(res.headers.get('Location'), DEMO_PATH);
  assertIsolated(res);
  const query = await handle(new Request('https://fua.example/?map=MAP01'), fakeAssets({}));
  assert.equal(query.headers.get('Location'), `${DEMO_PATH}?map=MAP01`);
});

test('a file the assets hold comes back as it was, isolated', async () => {
  const assets = fakeAssets({ '/demo/fua.js': 'js' });
  const res = await handle(new Request('https://fua.example/demo/fua.js'), assets);
  assert.equal(res.status, 200);
  assert.equal(await res.text(), 'js');
  assert.equal(res.headers.get('ETag'), '"e1"');
  assert.equal(res.headers.get('Content-Encoding'), null);
  assertIsolated(res);
  assert.deepEqual(assets.asked, ['/demo/fua.js']);
});

// [rc4l] freedoom2.wad is 27.5 MiB, over Workers' 25 MiB asset limit, so the stage keeps it gzipped and the browser unpacks it.
test('a file staged gzipped is sent as stored, typed as the file it was', async () => {
  const assets = fakeAssets({ '/demo/data/freedoom2.wad.gz': 'gz bytes' });
  const res = await handle(new Request('https://fua.example/demo/data/freedoom2.wad'), assets);
  assert.equal(res.status, 200);
  assert.equal(res.headers.get('Content-Encoding'), 'gzip');
  assert.equal(res.headers.get('Content-Type'), 'application/octet-stream');
  assertIsolated(res);
  assert.deepEqual(assets.asked, ['/demo/data/freedoom2.wad', '/demo/data/freedoom2.wad.gz']);
});

// [rc4l] A revisit asks whether freedoom2.wad changed, and the .gz's 304 was turned into the missing name's 404, so a cached page failed to start.
test('a revisit to a file staged gzipped gets its 304, not the 404', async () => {
  const assets = fakeAssets({ '/demo/data/freedoom2.wad.gz': 'gz bytes' });
  const res = await handle(new Request('https://fua.example/demo/data/freedoom2.wad', { headers: { 'If-None-Match': '"e1"' } }), assets);
  assert.equal(res.status, 304);
  assert.equal(res.headers.get('ETag'), '"e1"');
  assert.equal(res.headers.get('Content-Encoding'), null);
  assertIsolated(res);
});

test('a file missing both ways is the first 404, isolated', async () => {
  const res = await handle(new Request('https://fua.example/demo/none.pk3'), fakeAssets({}));
  assert.equal(res.status, 404);
  assert.equal(await res.text(), 'nope');
  assertIsolated(res);
});

test('isolating keeps the status and lets extra headers override', async () => {
  const res = isolated(new Response(null, { status: 307, headers: { Location: '/demo/', 'Cross-Origin-Resource-Policy': 'cross-origin' } }), { 'X-Extra': '1' });
  assert.equal(res.status, 307);
  assert.equal(res.headers.get('Location'), '/demo/');
  assert.equal(res.headers.get('X-Extra'), '1');
  assertIsolated(res);
});

test('the Worker answers through its ASSETS binding', async () => {
  const res = await worker.fetch(new Request('https://fua.example/demo/fua.js'), { ASSETS: fakeAssets({ '/demo/fua.js': 'js' }) });
  assert.equal(await res.text(), 'js');
  assertIsolated(res);
});
