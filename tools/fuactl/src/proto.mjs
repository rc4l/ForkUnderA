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

// Allocate a candidate bridge port deterministically from a base + offset, wrapping in the ephemeral
// range. Kept pure so port selection is testable; the caller confirms the port is actually free.
export function candidatePort(base, offset) {
  const lo = 20000, span = 20000;
  return lo + (((base - lo) + offset) % span + span) % span;
}
