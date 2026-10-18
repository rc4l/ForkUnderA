// [rc4l] The sim trace (mcp_simtrace.cpp): each tic's fingerprints and every damage, kill and spawn, and where two runs' traces part.

// [rc4l] The fingerprint kinds a trace line can carry, H the world and the rest what mcp_simtrace.cpp folds apart from it.
export const KINDS = { H: "sim", R: "random streams", A: "ACS variables", P: "players", L: "seen bits", T: "lights, sequences and quakes" };
// [rc4l] Kinds that differ between two runs of one build, since the frames drawn between tics set them, so a difference is told and not judged.
export const FRAME_DEPENDENT = new Set(["L"]);

// [rc4l] Each level's tics are numbered from the start again, so a tic past the first level keeps apart as this many tics later.
const LEVEL_SPAN = 10000000;

// [rc4l] A tic as the trace names it, with the level it is in once there was more than one.
export function formatTic(key) {
  const level = Math.floor(key / LEVEL_SPAN);
  return level ? `${key % LEVEL_SPAN} of level ${level + 1}` : `${key}`;
}

// [rc4l] A trace file's lines as each tic's fingerprints by kind (hashes is the world's) and the events in order; a world tic lower than the last starts the next level.
export function parseSimTrace(text) {
  const kinds = new Map();
  const events = [];
  let level = 0, lastWorld = null;
  for (const line of String(text).split(/\r?\n/)) {
    const m = /^(-?\d+) ([A-Z]) (\d+)$/.exec(line.trim());
    if (m && KINDS[m[2]]) {
      const tic = Number(m[1]);
      if (m[2] === "H") {
        if (lastWorld !== null && tic < lastWorld) level++;
        lastWorld = tic;
      }
      if (!kinds.has(m[2])) kinds.set(m[2], new Map());
      kinds.get(m[2]).set(level * LEVEL_SPAN + tic, m[3]);
    } else if (line.trim()) events.push(line.trim());
  }
  return { hashes: kinds.get("H") || new Map(), kinds, events };
}

// [rc4l] The first tic two fingerprint series both hold with different values, null when none does.
function firstDiffering(a, b) {
  const tics = [...a.keys()].filter((t) => b.has(t)).sort((x, y) => x - y);
  for (const t of tics) if (a.get(t) !== b.get(t)) return { tic: t, a: a.get(t), b: b.get(t) };
  return null;
}

// [rc4l] Where two traces part: per kind both carry, the first tic with different fingerprints (firstTic the world's), and the first event line that differs.
export function diffSimTraces(a, b) {
  const tics = [...a.hashes.keys()].filter((t) => b.hashes.has(t)).length;
  const parts = [];
  for (const kind of Object.keys(KINDS)) {
    const ka = a.kinds.get(kind), kb = b.kinds.get(kind);
    if (!ka || !kb) continue;
    const first = firstDiffering(ka, kb);
    if (first) parts.push({ kind, ...first });
  }
  const world = parts.find((p) => p.kind === "H");
  const firstTic = world ? { tic: world.tic, a: world.a, b: world.b } : null;
  let firstEvent = null;
  const n = Math.max(a.events.length, b.events.length);
  for (let i = 0; i < n; i++) {
    if (a.events[i] !== b.events[i]) { firstEvent = { index: i, a: a.events[i] ?? null, b: b.events[i] ?? null }; break; }
  }
  const compared = Object.keys(KINDS).filter((k) => a.kinds.has(k) && b.kinds.has(k));
  const judged = parts.filter((p) => !FRAME_DEPENDENT.has(p.kind));
  return { tics, events: [a.events.length, b.events.length], firstTic, parts, compared, firstEvent, same: judged.length === 0 && !firstEvent };
}

// [rc4l] The verdict tracediff prints, one line per finding.
export function describeSimTraceDiff(d) {
  const out = [];
  if (d.same) {
    const extra = d.compared.filter((k) => k !== "H" && !d.parts.some((p) => p.kind === k)).map((k) => KINDS[k]);
    out.push(`the traces agree: ${d.tics} tics hashed alike${extra.length ? ` (with ${extra.join(", ")})` : ""}, ${d.events[0]} events in the same order`);
    for (const p of d.parts) out.push(`the ${KINDS[p.kind]} part at tic ${formatTic(p.tic)}, which the frames between tics set, so it is not judged`);
    return out;
  }
  for (const p of d.parts) {
    out.push(p.kind === "H" ? `the sim parts at tic ${formatTic(p.tic)}: fingerprint ${p.a} against ${p.b}` : `the ${KINDS[p.kind]} part at tic ${formatTic(p.tic)}: ${p.a} against ${p.b}`);
  }
  if (d.firstEvent) out.push(`the first event that differs is #${d.firstEvent.index}: ${d.firstEvent.a ?? "(none)"} against ${d.firstEvent.b ?? "(none)"}`);
  return out;
}
