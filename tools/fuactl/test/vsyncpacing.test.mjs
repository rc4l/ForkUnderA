import { test } from "node:test";
import assert from "node:assert/strict";
import { buildVsyncPacingWad, pacingOf, inRhythm, judgePacing, STALLS_US } from "../scenarios/vsyncpacing.mjs";

const rows = (times, vsync = 1) => times.map((ms) => ({ "frame/ms": ms, "frame/vsync": vsync }));
const even = pacingOf(rows(Array(144).fill(6.94)));
const rhythm = pacingOf(rows(Array(48).fill([1.1, 5.7, 13.9]).flat()));

test("the map builds with a player start and every stall is a real one", () => {
  assert.ok(buildVsyncPacingWad().toString("latin1").includes("type = 1;"));
  assert.ok(STALLS_US.length >= 20 && STALLS_US.every((us) => us > 0));
});

test("an even second has no short frames and the 1/6/14 rhythm a third", () => {
  assert.equal(even.short, 0);
  assert.equal(inRhythm(even), false);
  assert.ok(Math.abs(rhythm.mean - 6.9) < 0.01);
  assert.ok(Math.abs(rhythm.short - 1 / 3) < 1e-9);
  assert.equal(inRhythm(rhythm), true);
});

test("a second with no frames or with vsync off is read as such", () => {
  assert.deepEqual(pacingOf([]), { frames: 0, mean: 0, short: 0, vsync: false });
  assert.equal(pacingOf(rows([6.94, 0], 1)).frames, 1);
  assert.equal(pacingOf(rows([2, 2], 0)).vsync, false);
});

test("stalls that leave the frames even pass", () => {
  const r = judgePacing("x", [{ stall: 12000, first: even, after: null }]);
  assert.equal(r.failed, 0);
  assert.equal(r.line, "PASS x: 1 stalls at a 6.94 ms refresh, none left the frames in a rhythm");
});

// [Doruk-Aksoy] The fixed build blipped once in ninety stalls and was even again the next second.
test("a rhythm that recovers the next second passes and is counted", () => {
  const r = judgePacing("x", [{ stall: 12000, first: rhythm, after: even }, { stall: 9000, first: even, after: null }]);
  assert.equal(r.failed, 0);
  assert.match(r.line, /\(1 recovered within a second\)$/);
});

test("a rhythm that holds for both seconds fails, naming the stall", () => {
  const r = judgePacing("x", [{ stall: 9000, first: even, after: null }, { stall: 12000, first: rhythm, after: rhythm }]);
  assert.equal(r.failed, 1);
  assert.equal(r.line, "FAIL x: 1 of 2 stalls left the frames in an uneven rhythm, 33% then 33% of frames short after the 12000 us one (mean 6.94 ms)");
});

test("no trials, vsync off or an empty recording fail", () => {
  assert.equal(judgePacing("x", []).line, "FAIL x: no stall was measured");
  const off = pacingOf(rows([2, 2], 0));
  assert.equal(judgePacing("x", [{ stall: 8000, first: off, after: null }]).line, "FAIL x: vsync was off after the 8000 us stall");
  assert.equal(judgePacing("x", [{ stall: 8000, first: rhythm, after: off }]).failed, 1);
  const none = { frames: 0, mean: 0, short: 0, vsync: true };
  assert.equal(judgePacing("x", [{ stall: 8000, first: none, after: null }]).line, "FAIL x: no frames were recorded after the 8000 us stall");
});
