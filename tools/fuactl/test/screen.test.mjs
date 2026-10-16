import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { parseCapture, analyzeCapture, formatCapture, parseTrace, alignTrace, formatTrace, captureScreen } from "../src/screen.mjs";

// [rc4l] A 480 Hz capture: every refresh a new image, except where a refresh is skipped.
function rows(gapsInRefreshes, period = 1e6 / 480) {
  const out = [{ us: 0, changed: true, frames: 1 }];
  let t = 0;
  for (const g of gapsInRefreshes) { t += g * period; out.push({ us: Math.round(t), changed: true, frames: 1 }); }
  return out;
}

test("the capture's rows are its frames, changed or not, each one desktop frame unless it says more", () => {
  assert.deepEqual(parseCapture("present_us,changed,frames\n0,1,1\n2083,0,1\n6249,1,2\n"), [
    { us: 0, changed: true, frames: 1 }, { us: 2083, changed: false, frames: 1 }, { us: 6249, changed: true, frames: 2 }]);
  assert.deepEqual(parseCapture("present_us,changed\n0,1\n"), [{ us: 0, changed: true, frames: 1 }]);
});

// [rc4l] A capture that fell behind folds two desktop frames into one row, which looks like a stall and is not one.
test("a gap the capture did not see whole is no stall, and its folded frames are counted", () => {
  const r = rows([1, 2, 1, 2]);
  r[2].frames = 2;
  r.splice(4, 0, { us: r[4].us - 1000, changed: false, frames: 3 });
  const a = analyzeCapture(r, 480);
  assert.equal(a.stallsPerSec, 0);
  assert.ok(Math.abs(a.missedPerSec * a.seconds - 3) < 1e-9);
  assert.match(formatCapture(a), /the capture missed [0-9.]+ frames\/s$/);
  assert.doesNotMatch(formatCapture(analyzeCapture(rows([1, 1]), 480)), /missed/);
});

test("a capture with every refresh new shows the refresh rate and no stalls", () => {
  const a = analyzeCapture(rows(Array(480).fill(1)), 480);
  assert.ok(Math.abs(a.shownPerSec - 480) < 1);
  assert.ok(a.repeatedPerSec < 1);
  assert.equal(a.stallsPerSec, 0);
  assert.ok(Math.abs(a.gapMs.p50 - 2.083) < 0.01);
});

// [rc4l] The judder measured on Sunder MAP14: images arriving one, two and three refreshes apart.
test("skipped refreshes show as fewer images shown and stalls a second", () => {
  const gaps = [];
  for (let i = 0; i < 100; i++) gaps.push(1, 1, 2, 1, 3);
  const a = analyzeCapture(rows(gaps), 480);
  assert.ok(Math.abs(a.shownPerSec - 480 * 5 / 8) < 1);
  assert.ok(Math.abs(a.repeatedPerSec - 480 * 3 / 8) < 1);
  assert.ok(Math.abs(a.stallsPerSec - 200 / (800 / 480)) < 0.5);
  assert.ok(Math.abs(a.gapMs.max - 6.25) < 0.01);
  assert.match(formatCapture(a), /^300 shown\/s of 480 Hz, 180 repeated\/s, 120\.0 stalls\/s/);
});

test("frames whose patch did not change are not images shown, and too little says nothing", () => {
  const r = rows([1, 1, 1]);
  r.splice(2, 0, { us: r[1].us + 500, changed: false });
  assert.ok(analyzeCapture(r, 480).shownPerSec > 0);
  assert.equal(analyzeCapture([{ us: 0, changed: true }], 480), null);
  assert.match(formatCapture({ ...analyzeCapture([{ us: 0, changed: true }], 480) }), /never changed/);
  assert.match(formatCapture({ ...analyzeCapture(rows([1, 1]), 480), covered: true }), /^another window covered/);
  assert.equal(analyzeCapture(rows([1, 1]), 0), null);
});

test("a trace is each presenting frame's view and present times, and one without them is nothing", () => {
  assert.deepEqual(parseTrace("us,deg,view_at,present_at\n0,1,100,150\n5,1,200,0\n9,1,300,350\n"), [
    { viewAt: 100, presentAt: 150 }, { viewAt: 300, presentAt: 350 }]);
  assert.deepEqual(parseTrace("us,deg,tic\n0,1,0\n"), []);
});

// [rc4l] Frames sampled 2 ms apart but taking 1 then 3 ms to present: each image's content is a different age when it is seen, which is judder.
test("images are matched to the frame that presented last before them, and uneven ages are judder", () => {
  const even = [], uneven = [];
  for (let i = 0; i < 20; i++) {
    even.push({ viewAt: i * 2000, presentAt: i * 2000 + 1500 });
    uneven.push({ viewAt: i * 2000, presentAt: i * 2000 + (i % 2 ? 1900 : 1000) });
  }
  const rows = [];
  for (let i = 0; i < 20; i++) rows.push({ us: i * 2000 + 1950, changed: true, frames: 1 });
  rows.push({ us: 5, changed: false, frames: 1 }, { us: 99999, changed: true, frames: 1 });
  const a = alignTrace(even, rows, 0);
  assert.equal(a.images, 19);
  assert.ok(Math.abs(a.latencyMs.p50 - 1.95) < 1e-9);
  assert.equal(a.judderMs.mean, 0);
  const b = alignTrace(uneven, rows, 0);
  assert.equal(b.judderMs.mean, 0);
  const late = rows.map((r, i) => ({ ...r, us: r.us + (i % 2 ? 0 : 1000) }));
  const c2 = alignTrace(even, late, 0);
  assert.ok(c2.judderMs.mean > 0.9);
  assert.match(formatTrace(c2), /^\d+ images reached the screen p50 [0-9.]+ ms \(p99 [0-9.]+\) after their view was set up; judder/);
  assert.equal(alignTrace(even.slice(0, 1), rows, 0), null);
  assert.equal(alignTrace(even, rows.slice(0, 2), 0), null);
});

test("a capture orbits the aim while it watches and stops it after, even when the helper fails", async () => {
  const sent = [];
  const exec = async (_c, text) => { sent.push(text); return ""; };
  const out = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-sc-")), "cap.csv");
  const run = (exe, args) => {
    fs.writeFileSync(args[2], "present_us,changed,frames\n0,1,1\n2083,1,1\n4166,1,1\n");
    return { status: 0, stdout: '{"refreshHz":480,"patch":[1,2,256,256],"startUs":99,"covered":0}\n' };
  };
  const r = await captureScreen({}, { pid: 7, exe: "sc.exe", out, exec, run, windowClass: "FuaDiligentWnd" });
  assert.deepEqual(sent, ["fua_debug_orbit_period 1.5", "fua_debug_orbit 6", "fua_debug_orbit 0"]);
  assert.deepEqual(r.patch, [1, 2, 256, 256]);
  assert.equal(r.startUs, 99);
  assert.equal(r.covered, false);
  assert.ok(r.shownPerSec > 0);
  sent.length = 0;
  await assert.rejects(() => captureScreen({}, { pid: 7, exe: "sc.exe", out, exec, run: () => ({ status: 1, stderr: "no visible window for pid 7" }) }), /no visible window/);
  assert.deepEqual(sent, ["fua_debug_orbit_period 1.5", "fua_debug_orbit 6", "fua_debug_orbit 0"]);
  sent.length = 0;
  await captureScreen({}, { pid: 7, exe: "sc.exe", out, exec, run, orbit: 0 });
  assert.deepEqual(sent, []);
});

test("a traced capture arms the engine's trace, ends it, and lines it up, or stands alone without one", async () => {
  const sent = [];
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-sc-"));
  const out = path.join(dir, "cap.csv"), traceOut = path.join(dir, "trace.csv");
  const run = (exe, args) => {
    fs.writeFileSync(args[2], "present_us,changed,frames\n0,1,1\n2000,1,1\n4000,1,1\n6000,1,1\n8000,1,1\n");
    return { status: 0, stdout: '{"refreshHz":480,"patch":[1,2,256,256],"startUs":1000}\n' };
  };
  const exec = async (_c, text) => {
    sent.push(text);
    if (text === "fua_viewtrace end") fs.writeFileSync(traceOut, "us,view_at,present_at\n0,0,500\n1,2000,2500\n2,4000,4500\n3,6000,6500\n4,8000,8500\n5,10000,10500\n");
  };
  const r = await captureScreen({}, { pid: 7, exe: "sc.exe", out, traceOut, exec, run, orbit: 0, sleep: async () => {} });
  assert.deepEqual(sent, [`fua_viewtrace 20000 "${traceOut}"`, "fua_viewtrace end"]);
  assert.equal(r.trace.images, 5);
  assert.equal(r.trace.judderMs.mean, 0);
  const none = await captureScreen({}, { pid: 7, exe: "sc.exe", out, traceOut, exec: async () => {}, run, orbit: 0, sleep: async () => {} });
  assert.equal(none.trace, undefined);
});
