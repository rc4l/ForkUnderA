// Pure, dependency-free core of fuactl: NDJSON framing, message classification, the desync verdict,
// and registry pruning. No sockets, no fs, no engine -- so it is unit-tested with `node --test`.

// Split an accumulating buffer into complete NDJSON lines. Returns {messages, rest}.
export function drainLines(buffer) {
  const messages = [];
  let rest = buffer;
  let nl;
  while ((nl = rest.indexOf("\n")) !== -1) {
    const line = rest.slice(0, nl).trim();
    rest = rest.slice(nl + 1);
    if (!line) continue;
    try {
      messages.push(JSON.parse(line));
    } catch {
      // ignore malformed line
    }
  }
  return { messages, rest };
}

// Frame one request object to a wire line.
export function frameRequest(id, cmd, args) {
  const o = { id, cmd };
  if (args && Object.keys(args).length) o.args = args;
  return JSON.stringify(o) + "\n";
}

// Classify an inbound message.
export function classify(msg) {
  if (msg && msg.t === "hello") return "hello";
  if (msg && msg.t === "event") return "event";
  if (msg && typeof msg.id === "number") return "response";
  return "other";
}

// The determinism/desync verdict across instances. `hashes` is an array of the sim.hash strings from
// each instance at the same leveltime. Returns { agree, distinct } -- agree=true means every instance
// produced the same fingerprint (no desync); distinct lists the unique values when they disagree.
export function desyncVerdict(hashes) {
  const distinct = [...new Set(hashes)];
  return { agree: distinct.length === 1, distinct };
}

// Given registry entries [{pid, port, ...}] and a liveness predicate, split into live vs stale so a
// reaper can signal the live ones and delete the dead entries. Pure: liveness is injected.
export function partitionRegistry(entries, isAlive) {
  const live = [];
  const stale = [];
  for (const e of entries) {
    if (e && typeof e.pid === "number" && isAlive(e.pid)) live.push(e);
    else stale.push(e);
  }
  return { live, stale };
}

// Classify for a MULTI-SESSION-SAFE reaper. `~/.forkundera/instances` is a single global registry
// shared by every fuactl/session on the machine, so a reaper must NEVER kill an instance another
// session is actively using. Using each entry's recorded launcher pid (ppid):
//   dead   : the engine pid is gone            -> just delete its stale file
//   orphan : engine alive, but its launcher (ppid>0) is dead  -> a true hanging instance, safe to kill
//   owned  : engine alive and its launcher is alive, OR ppid is untracked (<=0) -> LEAVE ALONE by default
// so `reap --kill` reaps only orphans; killing owned/untracked ones requires an explicit --all.
export function classifyInstances(entries, isAlive) {
  const dead = [], orphan = [], owned = [];
  for (const e of entries) {
    if (!e || typeof e.pid !== "number" || !isAlive(e.pid)) { dead.push(e); continue; }
    const ppid = e.ppid;
    if (Number.isInteger(ppid) && ppid > 0 && !isAlive(ppid)) orphan.push(e);
    else owned.push(e); // launcher alive, or no launcher recorded -> don't touch without --all
  }
  return { dead, orphan, owned };
}

// Allocate a candidate bridge port deterministically from a base + offset, wrapping in the ephemeral
// range. Kept pure so port selection is testable; the caller confirms the port is actually free.
export function candidatePort(base, offset) {
  const lo = 20000, span = 20000;
  return lo + (((base - lo) + offset) % span + span) % span;
}

// Parse the `dumphud` capture (mcp_hud.cpp) into structured on-screen content. Each line is
// "text <x> <y> <string>", "image <x> <y> <name>", or "msg <layer> <l> <t> <tics> <string>"; the
// header "MCP_HUD" and blanks are ignored. Returns { texts, images, msgs } with numeric coords, so a
// caller can navigate by label instead of by pixel-reading a screenshot.
export function parseHudDump(dump) {
  const texts = [], images = [], msgs = [];
  for (const raw of String(dump || "").split("\n")) {
    const line = raw.replace(/\r$/, "");
    if (!line || line === "MCP_HUD") continue;
    const sp = line.indexOf(" ");
    const kind = sp < 0 ? line : line.slice(0, sp);
    const rest = sp < 0 ? "" : line.slice(sp + 1);
    if (kind === "text") {
      const m = rest.match(/^(-?\d+) (-?\d+) (-?\d+) ?(.*)$/);
      if (m) texts.push({ x: Number(m[1]), y: Number(m[2]), w: Number(m[3]), text: m[4] });
    } else if (kind === "image") {
      const m = rest.match(/^(-?\d+) (-?\d+) (.*)$/);
      if (m) images.push({ x: Number(m[1]), y: Number(m[2]), name: m[3] });
    } else if (kind === "msg") {
      const m = rest.match(/^(\d+) (-?[\d.]+) (-?[\d.]+) (-?\d+) ?(.*)$/);
      if (m) msgs.push({ layer: Number(m[1]), left: Number(m[2]), top: Number(m[3]), tics: Number(m[4]), text: m[5] });
    }
  }
  return { texts, images, msgs };
}

// Merge fragments the engine drew as separate DrawText calls on the same baseline (e.g. "Popular" +
// " Co-op Maps") back into one label per row, in reading order (top-to-bottom, left-to-right). yTol
// groups baselines a couple of pixels apart. Returns [{ y, x, text }] -- the visible lines.
export function hudLines(texts, yTol = 3) {
  const rows = [];
  for (const t of [...texts].sort((a, b) => a.y - b.y || a.x - b.x)) {
    const row = rows.find((r) => Math.abs(r.y - t.y) <= yTol);
    if (row) { row.parts.push(t); row.y = Math.min(row.y, t.y); }
    else rows.push({ y: t.y, parts: [t] });
  }
  return rows.map((r) => {
    const parts = r.parts.sort((a, b) => a.x - b.x);
    const x = parts[0].x;
    const last = parts[parts.length - 1];
    return {
      y: r.y,
      x,
      w: last.x + (last.w || 0) - x, // full span of the merged fragments, for a centred click
      text: parts.map((p) => p.text).join("").replace(/\s+/g, " ").trim(),
    };
  });
}

// Find the on-screen label matching `needle` (case-insensitive substring) and return its anchor
// {x, y, text}, or null. Prefers the exact DRAWN FRAGMENT (its real x,y -- the right click target,
// e.g. "Complex Doom" at its own column) over the merged reading-order line, which shares a baseline
// with whatever sat beside it and would anchor at the wrong x.
export function findHudLabel(texts, needle) {
  const want = String(needle).toLowerCase();
  const frag = texts.find((t) => String(t.text).toLowerCase().includes(want));
  if (frag) return { x: frag.x, y: frag.y, w: frag.w || 0, text: frag.text, cx: frag.x + Math.round((frag.w || 0) / 2) };
  const line = hudLines(texts).find((l) => l.text.toLowerCase().includes(want));
  return line ? { ...line, cx: line.x + Math.round((line.w || 0) / 2) } : null;
}
