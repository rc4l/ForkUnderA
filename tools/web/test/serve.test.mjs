import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { resolveInRoot, createServer } from '../serve.mjs';
import { ISOLATION_HEADERS } from '../headers.mjs';
import { scratch } from './scratch.mjs';

test('a request resolves under root, a directory to its index, and an escape to nothing', () => {
  const root = path.resolve('/srv/web');
  assert.equal(resolveInRoot(root, '/forkundera.js?v=1'), path.join(root, 'forkundera.js'));
  assert.equal(resolveInRoot(root, '/'), path.join(root, 'index.html'));
  assert.equal(resolveInRoot(root, '/sub/'), path.join(root, 'sub', 'index.html'));
  assert.equal(resolveInRoot(root, '/../secret'), path.join(root, 'secret'));
  assert.equal(resolveInRoot(root, '/%2e%2e/%2e%2e/secret'), path.join(root, 'secret'));
  assert.equal(resolveInRoot(root, '/bad%zz'), null);
  assert.equal(resolveInRoot(root, '/a%00b'), null);
});

test('every response carries the isolation headers', async () => {
  const dir = scratch('fua-serve-');
  fs.writeFileSync(path.join(dir, 'index.html'), '<p>hi</p>');
  fs.mkdirSync(path.join(dir, 'sub'));
  fs.writeFileSync(path.join(dir, 'sub', 'index.html'), '<p>sub</p>');
  const server = createServer(dir);
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  try {
    const ok = await fetch(`${base}/`);
    assert.equal(ok.status, 200);
    assert.equal(await ok.text(), '<p>hi</p>');
    for (const [k, v] of Object.entries(ISOLATION_HEADERS)) assert.equal(ok.headers.get(k), v);
    const sub = await fetch(`${base}/sub`);
    assert.equal(await sub.text(), '<p>sub</p>');
    const missing = await fetch(`${base}/nope.wasm`);
    assert.equal(missing.status, 404);
    assert.equal(missing.headers.get('Cross-Origin-Embedder-Policy'), 'require-corp');
  } finally {
    server.close();
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
