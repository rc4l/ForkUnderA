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

// [rc4l] The helper's rows, each a composed desktop frame: when it was presented and whether the window's patch changed.
export function parseCapture(csv) {
  return String(csv).split(/\r?\n/).slice(1).filter(Boolean).map((l) => {
    const [us, changed] = l.split(",").map(Number);
    return { us, changed: changed === 1 };
  });
}

// [rc4l] The capture judged: new images shown a second, refreshes a second that repeated the last image, and stalls (a gap of 1.5 refreshes or more) a second.
export function analyzeCapture(rows, refreshHz) {
  const shown = rows.filter((r) => r.changed).map((r) => r.us);
  if (shown.length < 2 || !(refreshHz > 0)) return null;
  const seconds = (shown[shown.length - 1] - shown[0]) / 1e6;
  const period = 1e6 / refreshHz;
  const gaps = shown.slice(1).map((t, i) => t - shown[i]);
  const shownPerSec = (shown.length - 1) / seconds;
  return {
    seconds,
    refreshHz,
    shownPerSec,
    repeatedPerSec: Math.max(0, refreshHz - shownPerSec),
    stallsPerSec: gaps.filter((g) => g >= 1.5 * period).length / seconds,
    gapMs: { p50: percentile(gaps, 50) / 1000, p99: percentile(gaps, 99) / 1000, max: Math.max(...gaps) / 1000 },
  };
}

// [rc4l] The analysis as one line.
export function formatCapture(a) {
  return `${a.shownPerSec.toFixed(0)} shown/s of ${a.refreshHz} Hz, ${a.repeatedPerSec.toFixed(0)} repeated/s, ${a.stallsPerSec.toFixed(1)} stalls/s`
    + ` (gaps p50 ${a.gapMs.p50.toFixed(2)} p99 ${a.gapMs.p99.toFixed(2)} max ${a.gapMs.max.toFixed(2)} ms)`;
}

// [rc4l] Watch an instance's window for `seconds` with the aim orbiting `orbit` degrees (0 for none), and judge what reached the screen.
export async function captureScreen(c, { pid, exe, seconds = 8, orbit = 6, period = 1.5, windowClass = null, out, exec, run = spawnSync }) {
  if (orbit > 0) {
    await exec(c, `fua_debug_orbit_period ${period}`);
    await exec(c, `fua_debug_orbit ${orbit}`);
  }
  try {
    const r = run(exe, [String(pid), String(seconds), out, ...(windowClass ? [windowClass] : [])], { encoding: "utf8" });
    if (r.status !== 0) throw new Error(`screencap: ${(r.stderr || "").trim() || `exit ${r.status}`}`);
    const head = JSON.parse(String(r.stdout).split(/\r?\n/)[0]);
    return { ...analyzeCapture(parseCapture(fs.readFileSync(out, "utf8")), head.refreshHz), patch: head.patch };
  } finally {
    if (orbit > 0) await exec(c, "fua_debug_orbit 0");
  }
}
