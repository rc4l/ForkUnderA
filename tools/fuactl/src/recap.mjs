// [rc4l] A fua_capture recording read back: its frames second by second, and which counters its slow seconds spent more on than its fast ones.
import { percentile } from "./stats.mjs";

// [rc4l] The CSV fua_capture writes, as one object of numbers a frame, keyed by its header.
export function parseCaptureCsv(text) {
  const lines = String(text).split(/\r?\n/).filter((l) => l.length);
  if (!lines.length) return [];
  const head = lines[0].split(",");
  return lines.slice(1).map((l) => {
    const f = l.split(",");
    const row = {};
    head.forEach((h, i) => { row[h] = Number(f[i]) || 0; });
    return row;
  });
}

// [rc4l] One second's frames summed up: frames drawn, tics and those on the game thread, held frames, frame time p50, p99 and max.
function secondOf(frames) {
  const ms = frames.map((r) => r["frame/ms"] || 0);
  const sum = (k) => frames.reduce((n, r) => n + (r[k] || 0), 0);
  return { fps: frames.length, tics: sum("frame/tics"), threaded: sum("handoff/threaded_tics"), held: sum("handoff/held_frames"),
    p50: percentile(ms, 50), p99: percentile(ms, 99), max: Math.max(...ms), frames };
}

// [rc4l] The frames cut into whole seconds by their own frame times, a part second at the end left out.
export function captureSeconds(rows) {
  const out = [];
  let frames = [], ms = 0;
  for (const r of rows) {
    frames.push(r);
    ms += r["frame/ms"] || 0;
    if (ms >= 1000) { out.push(secondOf(frames)); frames = []; ms = 0; }
  }
  return out;
}

// [rc4l] Each counter's mean a frame over the frames of the seconds given.
function meansOf(seconds) {
  const frames = seconds.flatMap((s) => s.frames);
  const sums = {};
  for (const r of frames) for (const [k, v] of Object.entries(r)) sums[k] = (sums[k] || 0) + v;
  for (const k of Object.keys(sums)) sums[k] /= Math.max(1, frames.length);
  return sums;
}

// [rc4l] The counters that grew most a frame from the fast seconds to the slow ones, [name, fast, slow] biggest growth first; by default the slowest quarter against the fastest, by frames drawn.
export function slowSecondsGrew(seconds, { fast = null, slow = null, limit = 25, skip = /^(mem\/|os\/|census\/|time_ms$|frame$|handoff\/seam_violations$)|bytes$/ } = {}) {
  const byFps = [...seconds].sort((a, b) => b.fps - a.fps);
  const quarter = Math.max(1, Math.floor(seconds.length / 4));
  const f = meansOf(fast ?? byFps.slice(0, quarter));
  const s = meansOf(slow ?? byFps.slice(-quarter));
  return Object.keys(s).filter((k) => !skip.test(k)).map((k) => [k, f[k] || 0, s[k]])
    .filter(([, a, b]) => b - a > 0).sort((x, y) => (y[2] - y[1]) - (x[2] - x[1])).slice(0, limit);
}

// [rc4l] The recap as lines: a row a second, then what the slow seconds spent more on.
export function formatRecap(seconds, grew) {
  const n = (v) => v.toFixed(2).padStart(7);
  const lines = ["sec   fps  tics  thread  held    p50ms    p99ms    maxms"];
  seconds.forEach((s, i) => lines.push(`${String(i).padStart(3)} ${String(s.fps).padStart(5)} ${String(s.tics).padStart(5)} ${String(s.threaded).padStart(7)} ${String(s.held).padStart(5)}  ${n(s.p50)}  ${n(s.p99)}  ${n(s.max)}`));
  lines.push("", "grew a frame from the fast seconds to the slow ones:", `${"counter".padEnd(40)} ${"fast".padStart(10)} ${"slow".padStart(10)}`);
  for (const [k, a, b] of grew) lines.push(`${k.padEnd(40)} ${a.toFixed(3).padStart(10)} ${b.toFixed(3).padStart(10)}`);
  return lines;
}
