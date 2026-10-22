import { test } from 'node:test';
import assert from 'node:assert/strict';
import { heapGrowth, heapSummary } from '../heap.mjs';

// [rc4l] A snapshot as Chrome writes one: flat node fields, one type table, the names in a string table.
function snapshot(rows) {
  const strings = [];
  const at = (s) => { const i = strings.indexOf(s); return i >= 0 ? i : strings.push(s) - 1; };
  const types = ['hidden', 'array', 'string', 'object', 'code', 'closure', 'regexp', 'number', 'native'];
  const nodes = rows.flatMap(([type, name, size]) => [types.indexOf(type), at(name), 1, size, 0, 0, 0]);
  return { snapshot: { meta: { node_fields: ['type', 'name', 'id', 'self_size', 'edge_count', 'trace_node_id', 'detachedness'], node_types: [types] } }, nodes, strings };
}

test('a snapshot sums by constructor, by function, and by kind for the rest', () => {
  const s = heapSummary(snapshot([['object', 'Response', 100], ['object', 'Response', 50], ['closure', 'onFrame', 32], ['closure', '', 8], ['string', 'abc', 20], ['native', 'ArrayBuffer', 1000]]));
  assert.deepEqual(s.Response, { count: 2, bytes: 150 });
  assert.deepEqual(s['onFrame()'], { count: 1, bytes: 32 });
  assert.deepEqual(s['(anonymous)()'], { count: 1, bytes: 8 });
  assert.deepEqual(s['(string)'], { count: 1, bytes: 20 });
  assert.deepEqual(s.ArrayBuffer, { count: 1, bytes: 1000 });
});

test('growth lists what gained bytes, most first, and leaves out what did not', () => {
  const before = { Response: { count: 2, bytes: 2048 }, Gone: { count: 5, bytes: 500 }, Same: { count: 1, bytes: 10 }, Fewer: { count: 3, bytes: 1000 } };
  const after = { Response: { count: 10, bytes: 10240 }, '(string)': { count: 3, bytes: 3072 }, Same: { count: 1, bytes: 10 }, Fewer: { count: 2, bytes: 2024 } };
  assert.deepEqual(heapGrowth(before, after), ['heap +8.0 KB in +8 Response', 'heap +3.0 KB in +3 (string)', 'heap +1.0 KB in -1 Fewer']);
  assert.deepEqual(heapGrowth(before, after, 1), ['heap +8.0 KB in +8 Response']);
});
