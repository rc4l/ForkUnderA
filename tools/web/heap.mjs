// [rc4l] Chrome heap snapshots summed and compared, for what a page's JavaScript keeps (queue #69): probe.mjs --heap takes one at a soak's first judged mark and one at its end.

// [rc4l] A snapshot's bytes and objects grouped as DevTools' Summary groups them: an object by its constructor, a closure by its function, anything else by its kind in parentheses.
export function heapSummary(snapshot) {
  const { node_fields: fields, node_types: [types] } = snapshot.snapshot.meta;
  const width = fields.length, typeAt = fields.indexOf('type'), nameAt = fields.indexOf('name'), sizeAt = fields.indexOf('self_size');
  const { nodes, strings } = snapshot;
  const groups = {};
  for (let i = 0; i < nodes.length; i += width) {
    const type = types[nodes[i + typeAt]], name = strings[nodes[i + nameAt]];
    const key = type === 'object' || type === 'native' ? name : type === 'closure' ? `${name || '(anonymous)'}()` : `(${type})`;
    const g = groups[key] || (groups[key] = { count: 0, bytes: 0 });
    g.count++;
    g.bytes += nodes[i + sizeAt];
  }
  return groups;
}

// [rc4l] What grew from one summary to the next, the most bytes first, as lines naming each group's bytes and objects gained.
export function heapGrowth(before, after, top = 15) {
  return Object.keys(after)
    .map((k) => ({ k, bytes: after[k].bytes - (before[k]?.bytes || 0), count: after[k].count - (before[k]?.count || 0) }))
    .filter((g) => g.bytes > 0)
    .sort((a, b) => b.bytes - a.bytes)
    .slice(0, top)
    .map((g) => `heap +${(g.bytes / 1024).toFixed(1)} KB in ${g.count >= 0 ? '+' : ''}${g.count} ${g.k}`);
}
