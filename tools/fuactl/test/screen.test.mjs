import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { screencapPaths, needsBuild, ensureScreencap, parseCapture, analyzeCapture, formatCapture, captureScreen } from "../src/screen.mjs";

// [rc4l] A 480 Hz capture: every refresh a new image, except where a refresh is skipped.
function rows(gapsInRefreshes, period = 1e6 / 480) {
  const out = [{ us: 0, changed: true }];
  let t = 0;
  for (const g of gapsInRefreshes) { t += g * period; out.push({ us: Math.round(t), changed: true }); }
  return out;
}

test("the helper is built when missing or older than its source, and not otherwise", () => {
  const p = screencapPaths("/x");
  assert.equal(p.exe, path.join("/x", "build", "Release", "screencap.exe"));
  assert.equal(needsBuild(p, (f) => (f === p.exe ? null : 5)), true);
  assert.equal(needsBuild(p, (f) => (f === p.exe ? 4 : 5)), true);
  assert.equal(needsBuild(p, (f) => (f === p.exe ? 6 : 5)), false);
});

test("building runs cmake configure then build, and a failure names its errors", () => {
  const p = screencapPaths("/x");
  const calls = [];
  const exe = ensureScreencap("cmake", { paths: { ...p, exe: "/nowhere/screencap.exe" }, run: (cm, args) => { calls.push(args[0]); return { status: 0 }; } });
  assert.equal(exe, "/nowhere/screencap.exe");
  assert.deepEqual(calls, ["-S", "--build"]);
  assert.throws(() => ensureScreencap("cmake", { paths: { ...p, exe: "/nowhere/screencap.exe" }, run: () => ({ status: 1, stdout: "x\nerror C2065: nope\n" }) }), /error C2065/);
  const fresh = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-sc-")), "screencap.exe");
  fs.writeFileSync(fresh, "");
  assert.equal(ensureScreencap("cmake", { paths: { source: fresh, build: "/b", exe: fresh }, run: () => { throw new Error("must not build"); } }), fresh);
});

test("the capture's rows are its frames, changed or not", () => {
  assert.deepEqual(parseCapture("present_us,changed\n0,1\n2083,0\n4166,1\n"), [
    { us: 0, changed: true }, { us: 2083, changed: false }, { us: 4166, changed: true }]);
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
  assert.equal(analyzeCapture(rows([1, 1]), 0), null);
});

test("a capture orbits the aim while it watches and stops it after, even when the helper fails", async () => {
  const sent = [];
  const exec = async (_c, text) => { sent.push(text); return ""; };
  const out = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-sc-")), "cap.csv");
  const run = (exe, args) => {
    fs.writeFileSync(args[2], "present_us,changed\n0,1\n2083,1\n4166,1\n");
    return { status: 0, stdout: '{"refreshHz":480,"patch":[1,2,256,256]}\n' };
  };
  const r = await captureScreen({}, { pid: 7, exe: "sc.exe", out, exec, run, windowClass: "FuaDiligentWnd" });
  assert.deepEqual(sent, ["fua_debug_orbit_period 1.5", "fua_debug_orbit 6", "fua_debug_orbit 0"]);
  assert.deepEqual(r.patch, [1, 2, 256, 256]);
  assert.ok(r.shownPerSec > 0);
  sent.length = 0;
  await assert.rejects(() => captureScreen({}, { pid: 7, exe: "sc.exe", out, exec, run: () => ({ status: 1, stderr: "no visible window for pid 7" }) }), /no visible window/);
  assert.deepEqual(sent, ["fua_debug_orbit_period 1.5", "fua_debug_orbit 6", "fua_debug_orbit 0"]);
  sent.length = 0;
  await captureScreen({}, { pid: 7, exe: "sc.exe", out, exec, run, orbit: 0 });
  assert.deepEqual(sent, []);
});
