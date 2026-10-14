// [rc4l] "Is the engine actually alive?" and "is this engine even driveable?" -- the two questions
// that cost the most time when an instance stops answering.
//
// Why this exists: an engine that stops responding looks identical from the outside whether it is
// (a) gone, (b) spinning inside one function so it never polls the bridge again, (c) still polling
// but not advancing tics, or (d) simply paused. Telling those apart used to mean noticing an RPC
// timeout, guessing, attaching lldb by hand and reading a backtrace. `fuactl hang` does the whole
// sequence in one command and, when the engine is stuck, names the function it is stuck in --
// which is how the automap parchment-wrap hang was found (top of stack: AM_ScrollParchment).
//
// `fuactl doctor` answers the other one: the MCP can only drive a build compiled with
// -DFUA_MCP_BRIDGE=ON (ZX_MCP_BRIDGE=1 ./mac_compile.sh). A plain release build in the same path
// fails with "bridge port never opened", which reads like a crash and is not one.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
import fs from "node:fs";
import path from "node:path";

import { BridgeClient } from "./client.mjs";
import { readRegistry, pidAlive } from "./registry.mjs";
import { sampleProcess } from "./sample.mjs";

// The env var the engine's bridge reads at startup. Present in the binary iff FUA_MCP_BRIDGE was on.
export const BRIDGE_MARKER = "ZANDRONUM_BRIDGE_PORT";

// ---- Liveness classification (pure, unit-tested) ---------------------------

// Two tic readings taken `gap` apart, plus whether the process and the bridge answered, are enough
// to separate the four failure modes. `paused` is the engine's own pause flag (-1 = unknown).
export function classifyLiveness({ processAlive, connected, ticA = null, ticB = null, paused = -1 }) {
  if (!processAlive) {
    return { state: "gone", healthy: false, summary: "the process is not running -- it exited or was killed" };
  }
  if (!connected) {
    return {
      state: "unreachable",
      healthy: false,
      summary:
        "the process is alive but the bridge never answered -- either the game thread is stuck before it polls again, or this build has no bridge (see `fuactl doctor`)",
    };
  }
  if (ticA === null || ticB === null) {
    return { state: "unknown", healthy: false, summary: "connected, but sim.tic did not return a tic" };
  }
  if (ticB > ticA) {
    return { state: "healthy", healthy: true, summary: `ticking (${ticA} -> ${ticB})` };
  }
  if (paused > 0) {
    return { state: "paused", healthy: true, summary: `not ticking because the game is paused (tic ${ticB})` };
  }
  return {
    state: "stalled",
    healthy: false,
    summary: `the bridge answers but the tic is frozen at ${ticB} -- the game loop is running and the simulation is not`,
  };
}

// ---- Hang diagnosis --------------------------------------------------------

function sleep(ms) { return new Promise((r) => setTimeout(r, ms)); }

async function realProbe(port, token, gapMs, connectTimeoutMs) {
  const c = new BridgeClient();
  try {
    await c.connect(Number(port), { token: token || null, timeoutMs: connectTimeoutMs });
    await c.waitHello(connectTimeoutMs);
    const a = await c.rpc("sim.tic", {}, connectTimeoutMs);
    await sleep(gapMs);
    const b = await c.rpc("sim.tic", {}, connectTimeoutMs);
    return { connected: true, ticA: a?.gametic ?? null, ticB: b?.gametic ?? null, paused: b?.paused ?? -1 };
  } catch (e) {
    return { connected: false, error: e.message };
  } finally {
    try { c.close(); } catch { /* already gone */ }
  }
}

// Probe an instance and, when it is not healthy, sample it so the report names the function the
// game thread is stuck in. `_probe`/`_sample`/`_pidAlive` are test seams.
export async function diagnoseHang({
  port = null,
  token = null,
  pid = null,
  gapMs = 1500,
  connectTimeoutMs = 4000,
  sampleSeconds = 2,
  _probe = realProbe,
  _sample = sampleProcess,
  _pidAlive = pidAlive,
  _registry = readRegistry,
} = {}) {
  let resolvedPid = pid;
  if (!resolvedPid && port) {
    const e = _registry().find((x) => String(x.port) === String(port));
    if (e) resolvedPid = e.pid;
  }

  const processAlive = resolvedPid ? _pidAlive(resolvedPid) : true;
  const probe = processAlive && port
    ? await _probe(port, token, gapMs, connectTimeoutMs)
    : { connected: false, error: port ? "process is gone" : "no --port given" };

  const verdict = classifyLiveness({ processAlive, ...probe });
  const report = { pid: resolvedPid ?? null, port: port ?? null, ...verdict, ...probe };

  // Only sample when something is actually wrong, and only when we know which process to sample.
  if (!verdict.healthy && processAlive && resolvedPid) {
    const s = await _sample(resolvedPid, { seconds: sampleSeconds, top: 8, engineOnly: true });
    report.stack = s;
    if (s.available && s.functions.length) {
      report.stuckIn = s.functions[0].symbol;
      report.summary += ` -- top of stack: ${s.functions[0].symbol}`;
    } else if (s.available) {
      // A spinning thread samples; one BLOCKED on a lock/condvar (or a stopped process) does not.
      // No samples is itself a finding -- it says "waiting", not "looping".
      report.stack.note = "no samples: the thread is blocked or stopped rather than spinning -- `lldb -p <pid> -b -o 'bt all'` for the wait it is parked on";
      report.summary += " -- no samples, so it is blocked rather than looping";
    }
  }
  return report;
}

// ---- Engine doctor ---------------------------------------------------------

// Newest mtime under `dir` (ms), or 0 if it cannot be walked. Skips dot-dirs and build output.
export function newestMtime(dir, { _fs = fs } = {}) {
  let newest = 0;
  const walk = (d) => {
    let entries;
    try { entries = _fs.readdirSync(d, { withFileTypes: true }); } catch { return; }
    for (const e of entries) {
      if (e.name.startsWith(".") || e.name === "build" || e.name === "node_modules") continue;
      const p = path.join(d, e.name);
      if (e.isDirectory()) { walk(p); continue; }
      try {
        const m = _fs.statSync(p).mtimeMs;
        if (m > newest) newest = m;
      } catch { /* raced away */ }
    }
  };
  walk(dir);
  return newest;
}

// Report whether the configured engine binary can be driven at all, and whether it predates the
// source. Reads the binary once and looks for the bridge's env-var string.
export function doctorEngine({ exe = null, label = "engine", srcDir = null, _fs = fs } = {}) {
  const out = { label, exe, exists: false, hasBridge: false, mtimeMs: null, newestSourceMtimeMs: null, stale: false, problems: [] };
  if (!exe) {
    out.problems.push("no engine path -- set FUACTL_ENGINE (fuactl) or ZANDRONUM_EXE (MCP)");
    return out;
  }
  let buf;
  try {
    buf = _fs.readFileSync(exe);
    out.exists = true;
    out.mtimeMs = _fs.statSync(exe).mtimeMs;
  } catch {
    out.problems.push(`no engine binary at ${exe}`);
    return out;
  }

  out.hasBridge = buf.includes(BRIDGE_MARKER);
  if (!out.hasBridge) {
    out.problems.push(
      "this build has no MCP bridge, so nothing can drive it (launches fail with 'bridge port never opened'). Rebuild with: ZX_MCP_BRIDGE=1 ./mac_compile.sh",
    );
  }

  if (srcDir) {
    out.newestSourceMtimeMs = newestMtime(srcDir, { _fs });
    out.stale = out.newestSourceMtimeMs > 0 && out.mtimeMs < out.newestSourceMtimeMs;
    if (out.stale) {
      out.problems.push("the engine binary is older than the source tree -- you are driving a stale build");
    }
  }
  return out;
}
