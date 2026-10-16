// [rc4l] The sim trace (mcp_simtrace.cpp): each tic's fingerprint and every damage, kill and spawn, and where two runs' traces part.

// [rc4l] A trace file's lines as each tic's fingerprint and the events in order.
export function parseSimTrace(text) {
  const hashes = new Map();
  const events = [];
  for (const line of String(text).split(/\r?\n/)) {
    const m = /^(-?\d+) H (\d+)$/.exec(line.trim());
    if (m) hashes.set(Number(m[1]), m[2]);
    else if (line.trim()) events.push(line.trim());
  }
  return { hashes, events };
}

// [rc4l] Where two traces part: the first tic both hashed with different fingerprints, and the first event line that differs, each null when none does.
export function diffSimTraces(a, b) {
  const tics = [...a.hashes.keys()].filter((t) => b.hashes.has(t)).sort((x, y) => x - y);
  let firstTic = null;
  for (const t of tics) {
    if (a.hashes.get(t) !== b.hashes.get(t)) { firstTic = { tic: t, a: a.hashes.get(t), b: b.hashes.get(t) }; break; }
  }
  let firstEvent = null;
  const n = Math.max(a.events.length, b.events.length);
  for (let i = 0; i < n; i++) {
    if (a.events[i] !== b.events[i]) { firstEvent = { index: i, a: a.events[i] ?? null, b: b.events[i] ?? null }; break; }
  }
  return { tics: tics.length, events: [a.events.length, b.events.length], firstTic, firstEvent, same: !firstTic && !firstEvent };
}

// [rc4l] The verdict tracediff prints, one line per finding.
export function describeSimTraceDiff(d) {
  if (d.same) return [`the traces agree: ${d.tics} tics hashed alike, ${d.events[0]} events in the same order`];
  const out = [];
  if (d.firstTic) out.push(`the sim parts at tic ${d.firstTic.tic}: fingerprint ${d.firstTic.a} against ${d.firstTic.b}`);
  if (d.firstEvent) out.push(`the first event that differs is #${d.firstEvent.index}: ${d.firstEvent.a ?? "(none)"} against ${d.firstEvent.b ?? "(none)"}`);
  return out;
}
