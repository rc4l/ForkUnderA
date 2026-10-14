// [rc4l] fuactl screen: what reaches the screen of a running instance, judged from the compositor's own frames rather than the engine's.
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { percentile } from "./stats.mjs";

const HERE = fileURLToPath(new URL("../native/screencap/", import.meta.url));

// [rc4l] Where the helper's source, build tree and executable live.
export function screencapPaths(dir = HERE) {
  return { source: path.join(dir, "screencap.cpp"), build: path.join(dir, "build"), exe: path.join(dir, "build", "Release", "screencap.exe") };
}

// [rc4l] Whether the helper must be built again: it is missing, or older than its source.
export function needsBuild(paths, stat = (p) => (fs.existsSync(p) ? fs.statSync(p).mtimeMs : null)) {
  const exe = stat(paths.exe);
  return exe === null || exe < stat(paths.source);
}

// [rc4l] Build the helper with cmake when it needs it, and return its path.
export function ensureScreencap(cmake, { paths = screencapPaths(), run = spawnSync, log = () => {} } = {}) {
  if (!needsBuild(paths)) return paths.exe;
  log("building the screen capture helper");
  const dir = path.dirname(paths.source);
  for (const args of [["-S", dir, "-B", paths.build], ["--build", paths.build, "--config", "Release"]]) {
    const r = run(cmake, args, { encoding: "utf8" });
    if (r.status !== 0) throw new Error(`screencap build failed: ${((r.stdout || "") + (r.stderr || "")).split(/\r?\n/).filter((l) => /error/i.test(l)).slice(0, 5).join(" | ")}`);
  }
  return paths.exe;
}

// [rc4l] The helper's rows, each a composed desktop frame: when it was presented, whether the window's patch changed, and how many desktop frames it stands for.
export function parseCapture(csv) {
  return String(csv).split(/\r?\n/).slice(1).filter(Boolean).map((l) => {
    const [us, changed, frames] = l.split(",").map(Number);
    return { us, changed: changed === 1, frames: frames > 0 ? frames : 1 };
  });
}

// [rc4l] The capture judged: new images shown a second, refreshes a second that repeated the last image, stalls (a gap of 1.5 refreshes or more the capture saw whole) a second, and desktop frames a second the capture folded into the next because it fell behind.
export function analyzeCapture(rows, refreshHz) {
  const shown = [];
  let folded = 0, foldedSince = false;
  for (const r of rows) {
    folded += r.frames - 1;
    foldedSince = foldedSince || r.frames > 1;
    if (!r.changed) continue;
    shown.push({ us: r.us, whole: !foldedSince });
    foldedSince = false;
  }
  if (shown.length < 2 || !(refreshHz > 0)) return null;
  const seconds = (shown[shown.length - 1].us - shown[0].us) / 1e6;
  const period = 1e6 / refreshHz;
  const gaps = shown.slice(1).map((s, i) => ({ us: s.us - shown[i].us, whole: s.whole }));
  const us = gaps.map((g) => g.us);
  const shownPerSec = (shown.length - 1) / seconds;
  return {
    seconds,
    refreshHz,
    shownPerSec,
    repeatedPerSec: Math.max(0, refreshHz - shownPerSec),
    stallsPerSec: gaps.filter((g) => g.whole && g.us >= 1.5 * period).length / seconds,
    missedPerSec: folded / seconds,
    gapMs: { p50: percentile(us, 50) / 1000, p99: percentile(us, 99) / 1000, max: Math.max(...us) / 1000 },
  };
}

// [rc4l] The analysis as one line, or why there is none.
export function formatCapture(a) {
  if (a.covered) return "another window covered the engine's at the patch watched, so the capture saw that window: bring the engine's to the front and capture again";
  if (!(a.shownPerSec >= 0)) return "the patch watched never changed: the window was not drawing, or not where the capture looked";
  return `${a.shownPerSec.toFixed(0)} shown/s of ${a.refreshHz} Hz, ${a.repeatedPerSec.toFixed(0)} repeated/s, ${a.stallsPerSec.toFixed(1)} stalls/s`
    + ` (gaps p50 ${a.gapMs.p50.toFixed(2)} p99 ${a.gapMs.p99.toFixed(2)} max ${a.gapMs.max.toFixed(2)} ms)`
    + (a.missedPerSec > 0 ? `, the capture missed ${a.missedPerSec.toFixed(1)} frames/s` : "");
}

// [rc4l] An engine trace (fua_viewtrace's CSV): each frame's view set up and its present begun, on the capture's clock, for the frames that presented.
export function parseTrace(csv) {
  const lines = String(csv).split(/\r?\n/).filter(Boolean);
  const cols = (lines[0] || "").split(",");
  const view = cols.indexOf("view_at"), present = cols.indexOf("present_at");
  if (view < 0 || present < 0) return [];
  return lines.slice(1).map((l) => l.split(",").map(Number)).map((v) => ({ viewAt: v[view], presentAt: v[present] })).filter((f) => f.presentAt > 0);
}

// [rc4l] Each new image on screen against the engine frame it showed, the last to begin presenting before it: how long after its view was set up it reached the screen, and how much that changed from one image to the next, which the eye sees as judder at any frame rate.
export function alignTrace(frames, rows, startUs) {
  const f = [...frames].sort((a, b) => a.presentAt - b.presentAt);
  if (f.length < 2) return null;
  const latency = [];
  for (const r of rows) {
    const t = startUs + r.us;
    if (!r.changed || t <= f[0].presentAt || t > f[f.length - 1].presentAt) continue;
    let lo = 0, hi = f.length - 1;
    while (lo < hi) { const mid = (lo + hi + 1) >> 1; if (f[mid].presentAt < t) lo = mid; else hi = mid - 1; }
    latency.push(t - f[lo].viewAt);
  }
  if (latency.length < 3) return null;
  const change = latency.slice(1).map((l, i) => Math.abs(l - latency[i]));
  return {
    images: latency.length,
    latencyMs: { p50: percentile(latency, 50) / 1000, p99: percentile(latency, 99) / 1000 },
    judderMs: { mean: change.reduce((a, b) => a + b, 0) / change.length / 1000, p95: percentile(change, 95) / 1000 },
  };
}

// [rc4l] The alignment as one line.
export function formatTrace(t) {
  return `${t.images} images reached the screen p50 ${t.latencyMs.p50.toFixed(2)} ms (p99 ${t.latencyMs.p99.toFixed(2)}) after their view was set up;`
    + ` judder, that time's change from one image to the next: mean ${t.judderMs.mean.toFixed(3)} ms, p95 ${t.judderMs.p95.toFixed(3)} ms`;
}

// [rc4l] Watch an instance's window for `seconds` with the aim orbiting `orbit` degrees (0 for none), and judge what reached the screen; with `traceOut`, the engine's own frames over the same seconds are lined up against it.
export async function captureScreen(c, { pid, exe, seconds = 8, orbit = 6, period = 1.5, windowClass = null, out, traceOut = null, exec, run = spawnSync,
  sleep = (ms) => new Promise((r) => setTimeout(r, ms)) }) {
  if (orbit > 0) {
    await exec(c, `fua_debug_orbit_period ${period}`);
    await exec(c, `fua_debug_orbit ${orbit}`);
  }
  let result;
  try {
    if (traceOut) { fs.rmSync(traceOut, { force: true }); await exec(c, `fua_viewtrace 20000 "${traceOut}"`); }
    const r = run(exe, [String(pid), String(seconds), out, ...(windowClass ? [windowClass] : [])], { encoding: "utf8" });
    if (r.status !== 0) throw new Error(`screencap: ${(r.stderr || "").trim() || `exit ${r.status}`}`);
    const head = JSON.parse(String(r.stdout).split(/\r?\n/)[0]);
    const rows = parseCapture(fs.readFileSync(out, "utf8"));
    result = { ...analyzeCapture(rows, head.refreshHz), patch: head.patch, startUs: head.startUs, covered: head.covered === 1 };
    if (traceOut) {
      await exec(c, "fua_viewtrace end");
      for (let i = 0; i < 40 && !fs.existsSync(traceOut); i++) await sleep(100);
      // [rc4l] An engine without the trace's timing columns writes none of it, and the capture stands alone.
      if (fs.existsSync(traceOut)) result.trace = alignTrace(parseTrace(fs.readFileSync(traceOut, "utf8")), rows, head.startUs);
    }
  } finally {
    if (orbit > 0) await exec(c, "fua_debug_orbit 0");
  }
  return result;
}
