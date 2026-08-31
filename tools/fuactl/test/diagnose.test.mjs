import { test } from "node:test";
import assert from "node:assert/strict";
import { classifyLiveness, diagnoseHang, doctorEngine, newestMtime, BRIDGE_MARKER } from "../src/diagnose.mjs";

// ---- classifyLiveness ------------------------------------------------------

test("classifyLiveness: two advancing tics is healthy", () => {
  const r = classifyLiveness({ processAlive: true, connected: true, ticA: 100, ticB: 135, paused: 0 });
  assert.equal(r.state, "healthy");
  assert.equal(r.healthy, true);
});

test("classifyLiveness: a dead process is 'gone', not a hang", () => {
  const r = classifyLiveness({ processAlive: false, connected: false });
  assert.equal(r.state, "gone");
  assert.equal(r.healthy, false);
});

// This is the automap parchment-wrap shape: the process is fine, the game thread is spinning inside
// one function, so MCP_Bridge_Poll is never reached and every RPC times out.
test("classifyLiveness: alive but the bridge never answered is 'unreachable'", () => {
  const r = classifyLiveness({ processAlive: true, connected: false });
  assert.equal(r.state, "unreachable");
  assert.match(r.summary, /stuck before it polls|no bridge/);
});

// The other shape: the loop keeps running (bridge answers) but the tic counter is frozen.
test("classifyLiveness: bridge answers with a frozen tic is 'stalled'", () => {
  const r = classifyLiveness({ processAlive: true, connected: true, ticA: 4000, ticB: 4000, paused: 0 });
  assert.equal(r.state, "stalled");
  assert.equal(r.healthy, false);
});

test("classifyLiveness: a frozen tic while paused is benign", () => {
  const r = classifyLiveness({ processAlive: true, connected: true, ticA: 42, ticB: 42, paused: 1 });
  assert.equal(r.state, "paused");
  assert.equal(r.healthy, true);
});

test("classifyLiveness: connected but no tic in the reply is 'unknown'", () => {
  const r = classifyLiveness({ processAlive: true, connected: true, ticA: null, ticB: null });
  assert.equal(r.state, "unknown");
});

// ---- diagnoseHang ----------------------------------------------------------

test("diagnoseHang: an unreachable engine gets sampled and the stuck function is named", async () => {
  const r = await diagnoseHang({
    port: 7917,
    _registry: () => [{ pid: 4242, port: 7917 }],
    _pidAlive: () => true,
    _probe: async () => ({ connected: false, error: "rpc timeout: sim.tic" }),
    _sample: async () => ({ available: true, backend: "sample", functions: [{ symbol: "AM_ScrollParchment", binary: "forkundera", samples: 900 }] }),
  });
  assert.equal(r.state, "unreachable");
  assert.equal(r.pid, 4242);
  assert.equal(r.stuckIn, "AM_ScrollParchment");
  assert.match(r.summary, /AM_ScrollParchment/);
});

// A thread blocked on a condvar (the frozen-tic deadlock shape) produces no samples at all, which
// is a different finding from "spinning here" and must not read as "nothing wrong".
test("diagnoseHang: no samples means blocked, not looping", async () => {
  const r = await diagnoseHang({
    port: 7917, pid: 5,
    _pidAlive: () => true,
    _probe: async () => ({ connected: true, ticA: 900, ticB: 900, paused: 0 }),
    _sample: async () => ({ available: true, backend: "sample", functions: [] }),
  });
  assert.equal(r.state, "stalled");
  assert.equal(r.stuckIn, undefined);
  assert.match(r.stack.note, /blocked or stopped/);
  assert.match(r.summary, /blocked rather than looping/);
});

test("diagnoseHang: a healthy engine is not sampled", async () => {
  let sampled = false;
  const r = await diagnoseHang({
    port: 7917, pid: 10,
    _pidAlive: () => true,
    _probe: async () => ({ connected: true, ticA: 10, ticB: 45, paused: 0 }),
    _sample: async () => { sampled = true; return { available: true, functions: [] }; },
  });
  assert.equal(r.state, "healthy");
  assert.equal(sampled, false, "sampling a healthy engine wastes seconds for nothing");
  assert.equal(r.stuckIn, undefined);
});

test("diagnoseHang: a gone process is not probed or sampled", async () => {
  let probed = false;
  const r = await diagnoseHang({
    port: 7917, pid: 99,
    _pidAlive: () => false,
    _probe: async () => { probed = true; return { connected: true }; },
    _sample: async () => ({ available: true, functions: [] }),
  });
  assert.equal(r.state, "gone");
  assert.equal(probed, false);
  assert.equal(r.stack, undefined);
});

// ---- doctorEngine ----------------------------------------------------------

function fakeFs({ files = {}, dirs = {} } = {}) {
  return {
    readFileSync: (p) => { if (!(p in files)) throw new Error("ENOENT"); return Buffer.from(files[p].data); },
    statSync: (p) => { if (p in files) return { mtimeMs: files[p].mtime }; throw new Error("ENOENT"); },
    readdirSync: (d) => (dirs[d] || []),
  };
}

test("doctorEngine: a bridge-less build is the headline problem", () => {
  const _fs = fakeFs({ files: { "/e/zandronum": { data: "a release build with no bridge", mtime: 1000 } } });
  const r = doctorEngine({ exe: "/e/zandronum", _fs });
  assert.equal(r.exists, true);
  assert.equal(r.hasBridge, false);
  assert.match(r.problems[0], /ZX_MCP_BRIDGE=1/);
});

test("doctorEngine: a bridge build with no source dir has no problems", () => {
  const _fs = fakeFs({ files: { "/e/zandronum": { data: `armed via ${BRIDGE_MARKER} at startup`, mtime: 1000 } } });
  const r = doctorEngine({ exe: "/e/zandronum", _fs });
  assert.equal(r.hasBridge, true);
  assert.deepEqual(r.problems, []);
});

test("doctorEngine: a binary older than the source is reported stale", () => {
  const _fs = fakeFs({
    files: {
      "/e/zandronum": { data: BRIDGE_MARKER, mtime: 1000 },
      "/src/am_map.cpp": { data: "x", mtime: 5000 },
    },
    dirs: { "/src": [{ name: "am_map.cpp", isDirectory: () => false }] },
  });
  const r = doctorEngine({ exe: "/e/zandronum", srcDir: "/src", _fs });
  assert.equal(r.stale, true);
  assert.match(r.problems[0], /stale build/);
});

test("doctorEngine: a missing binary reports the path it looked at", () => {
  const r = doctorEngine({ exe: "/nope/zandronum", _fs: fakeFs() });
  assert.equal(r.exists, false);
  assert.match(r.problems[0], /\/nope\/zandronum/);
});

test("doctorEngine: no configured engine at all is its own problem", () => {
  const r = doctorEngine({ exe: null, _fs: fakeFs() });
  assert.match(r.problems[0], /FUACTL_ENGINE|ZANDRONUM_EXE/);
});

test("newestMtime: walks nested dirs and skips build output", () => {
  const _fs = fakeFs({
    files: { "/src/a.cpp": { data: "", mtime: 10 }, "/src/sub/b.cpp": { data: "", mtime: 77 }, "/src/build/c.o": { data: "", mtime: 999 } },
    dirs: {
      "/src": [
        { name: "a.cpp", isDirectory: () => false },
        { name: "sub", isDirectory: () => true },
        { name: "build", isDirectory: () => true },
      ],
      "/src/sub": [{ name: "b.cpp", isDirectory: () => false }],
    },
  });
  assert.equal(newestMtime("/src", { _fs }), 77);
});
