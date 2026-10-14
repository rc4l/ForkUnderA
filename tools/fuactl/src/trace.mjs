// [rc4l] fuactl trace: fua_viewtrace recorded in a running engine and read back, every frame with its length, phases and mouse input.
import fs from "node:fs";
import { percentile } from "./stats.mjs";

// [rc4l] A frame whose sim phase ran this long waited for the next tic, which the engine does while paused, in a menu or the console, or unfocused.
export const TIC_WAIT_US = 10000;
// [rc4l] A stretch with no mouse input this long is one a player moving the mouse would notice.
export const MOUSE_GAP_MS = 100;
const PHASES = ["sim", "input", "display", "walk", "rebake", "actors", "backend", "upload", "lights", "present", "flip"];

// [rc4l] The CSV fua_viewtrace writes, each row its columns by name as numbers.
export function parseViewTrace(csv) {
  const lines = String(csv).split(/\r?\n/).filter(Boolean);
  const cols = (lines[0] || "").split(",");
  return lines.slice(1).map((l) => {
    const v = l.split(",");
    const row = {};
    cols.forEach((c, i) => { row[c] = Number(v[i]); });
    return row;
  });
}

// [rc4l] Runs of consecutive rows where `pred` holds, as when they began (seconds into the recording), how many rows and how long (ms, by `us`).
export function runsOf(rows, pred, minMs = 0) {
  const out = [];
  for (let i = 0; i < rows.length;) {
    if (!pred(rows[i])) { i++; continue; }
    let j = i;
    while (j < rows.length && pred(rows[j])) j++;
    const ms = ((j < rows.length ? rows[j].us : rows[j - 1].us + (rows[j - 1].frame_us || 0)) - rows[i].us) / 1000;
    if (ms >= minMs) out.push({ atS: rows[i].us / 1e6, rows: j - i, ms });
    i = j;
  }
  return out;
}

// [rc4l] Mean microseconds per phase over `rows`.
function meanPhases(rows) {
  const m = {};
  for (const p of PHASES) m[p] = rows.length ? rows.reduce((a, r) => a + (r[p] || 0), 0) / rows.length : 0;
  return m;
}

// [rc4l] What a recording says: frame lengths apart from tic waits, what the slow frames spent more on, where the engine sat waiting, and the mouse input it got.
export function analyzeViewTrace(rows) {
  const framed = rows.filter((r) => r.frame_us > 0);
  if (framed.length < 2) return null;
  const seconds = (rows[rows.length - 1].us - rows[0].us) / 1e6;
  const working = framed.filter((r) => r.sim < TIC_WAIT_US);
  const ms = working.map((r) => r.frame_us / 1000);
  const p50 = percentile(ms, 50);
  const slow = working.filter((r) => r.frame_us / 1000 > 2 * p50);
  const rest = working.filter((r) => r.frame_us / 1000 <= 2 * p50);
  // [rc4l] What a frame's top-level phases leave unexplained; display holds the scene phases, and input inside it is counted twice, so this can dip below zero.
  const outside = (r) => r.frame_us - r.sim - r.input - r.display;
  const active = (r) => r.active === 1 && r.grabbed === 1 && r.paused === 0 && r.menu === 0 && r.console === 0;
  const playing = rows.filter(active);
  const states = {};
  for (const r of rows) {
    const k = r.console ? "console" : r.menu ? "menu" : r.paused ? "paused" : !r.active ? "not focused" : !r.grabbed ? "mouse not held" : "playing";
    states[k] = (states[k] || 0) + 1;
  }
  return {
    frames: rows.length,
    seconds,
    fps: rows.length / Math.max(seconds, 1e-9),
    frameMs: { p50, p99: percentile(ms, 99), p999: percentile(ms, 99.9), max: Math.max(...ms) },
    outsideUs: working.reduce((a, r) => a + outside(r), 0) / working.length,
    slow: { overMs: 2 * p50, frames: slow.length, phases: meanPhases(slow), restPhases: meanPhases(rest),
      outsideUs: slow.length ? slow.reduce((a, r) => a + outside(r), 0) / slow.length : 0 },
    ticWaits: runsOf(rows, (r) => r.sim >= TIC_WAIT_US),
    states,
    mouse: {
      messages: rows.reduce((a, r) => a + (r.mouse_msgs || 0), 0),
      ignored: rows.reduce((a, r) => a + (r.mouse_ignored || 0), 0),
      keptFromView: rows.reduce((a, r) => a + (r.mouse_kept || 0), 0),
      perSecondPlaying: playing.length ? playing.reduce((a, r) => a + (r.mouse_msgs || 0), 0) / Math.max(playing.reduce((a, r) => a + (r.frame_us || 0), 0) / 1e6, 1e-9) : 0,
      gaps: runsOf(rows, (r) => active(r) && !r.mouse_msgs, MOUSE_GAP_MS),
      stillWhileMoving: runsOf(rows, (r) => active(r) && r.mouse_dx !== 0 && Math.abs(r.deg) < 1e-6, MOUSE_GAP_MS),
    },
  };
}

// [rc4l] The analysis as lines a person reads.
export function formatViewTrace(a) {
  if (!a) return ["the recording holds too few frames to say anything"];
  const f = (x) => x.toFixed(2);
  const top = (m, base) => PHASES.map((p) => [p, m[p] - (base ? base[p] : 0)]).filter(([, v]) => Math.abs(v) >= 20)
    .sort((x, y) => Math.abs(y[1]) - Math.abs(x[1])).slice(0, 5).map(([p, v]) => `${p} ${base && v > 0 ? "+" : ""}${Math.round(v)}`).join(", ");
  const lines = [
    `${a.frames} frames over ${a.seconds.toFixed(1)} s (${Math.round(a.fps)} a second); frames not waiting for a tic: p50 ${f(a.frameMs.p50)} p99 ${f(a.frameMs.p99)} p99.9 ${f(a.frameMs.p999)} max ${f(a.frameMs.max)} ms`,
    `  where the time goes on average (us): ${top(a.slow.restPhases)}; outside every phase ${Math.round(a.outsideUs)}`,
    a.slow.frames ? `  ${a.slow.frames} frames over ${f(a.slow.overMs)} ms spent more on: ${top(a.slow.phases, a.slow.restPhases)} (us against the rest), outside every phase ${Math.round(a.slow.outsideUs)}` : "  no frame took twice the median",
    `  states: ${Object.entries(a.states).map(([k, v]) => `${k} ${v}`).join(", ")}`,
    a.ticWaits.length ? `  waiting for a tic (paused, menu, console or unfocused): ${a.ticWaits.map((w) => `${w.atS.toFixed(1)} s for ${(w.ms / 1000).toFixed(1)} s`).join(", ")}` : "  never waiting for a tic",
    `  mouse: ${a.mouse.messages} messages (${Math.round(a.mouse.perSecondPlaying)} a second while playing), ${a.mouse.ignored} ignored, ${a.mouse.keptFromView} kept from the view by a pause, menu or console`,
  ];
  if (a.mouse.gaps.length) lines.push(`  no mouse input while playing for ${MOUSE_GAP_MS} ms or more: ${a.mouse.gaps.slice(0, 12).map((g) => `${g.atS.toFixed(2)} s ${Math.round(g.ms)} ms`).join(", ")}${a.mouse.gaps.length > 12 ? `, and ${a.mouse.gaps.length - 12} more` : ""}`);
  if (a.mouse.stillWhileMoving.length) lines.push(`  the mouse moved but the view did not: ${a.mouse.stillWhileMoving.map((g) => `${g.atS.toFixed(2)} s ${Math.round(g.ms)} ms`).join(", ")}`);
  return lines;
}

// [rc4l] Records fua_viewtrace into `out` through `exec` (a console command runner) for as long as `during` takes, `seconds` by default, and returns its rows.
export async function recordViewTrace(c, { seconds = 10, out, exec, wait = (ms) => new Promise((r) => setTimeout(r, ms)), during = () => wait(seconds * 1000),
  exists = fs.existsSync, read = (p) => fs.readFileSync(p, "utf8") }) {
  fs.rmSync(out, { force: true });
  await exec(c, `fua_viewtrace 300000 "${out}"`);
  try { await during(); } finally { await exec(c, "fua_viewtrace end"); }
  for (let i = 0; i < 40 && !exists(out); i++) await wait(250);
  if (!exists(out)) throw new Error(`fua_viewtrace wrote nothing to ${out}: is the engine drawing (an unfocused Vulkan-only window may not be)?`);
  return parseViewTrace(read(out));
}
