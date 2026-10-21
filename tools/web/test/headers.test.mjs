import { test } from 'node:test';
import assert from 'node:assert/strict';
import { contentType, ISOLATION_HEADERS } from '../headers.mjs';

test('wasm and the page get their own types, anything else is bytes', () => {
  assert.equal(contentType('a/forkundera.wasm'), 'application/wasm');
  assert.equal(contentType('INDEX.HTML'), 'text/html; charset=utf-8');
  assert.equal(contentType('fua_core.pk3'), 'application/octet-stream');
  assert.equal(contentType('data.d/files'), 'application/octet-stream');
  assert.equal(contentType('README'), 'application/octet-stream');
});

test('the isolation headers are the three a threaded page needs', () => {
  assert.deepEqual(Object.keys(ISOLATION_HEADERS).sort(), ['Cross-Origin-Embedder-Policy', 'Cross-Origin-Opener-Policy', 'Cross-Origin-Resource-Policy']);
});
