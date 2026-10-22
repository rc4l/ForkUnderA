import { test } from "node:test";
import assert from "node:assert/strict";
import { buildTicOverloadWad, FRAME_CEILING_MS, judgeTicOverload, statOf, TIC_US } from "../scenarios/ticoverload.mjs";

test("the tic overload map builds", () => {
  assert.ok(buildTicOverloadWad().length > 0);
});

test("frames keep coming while 40 ms tics slow the game to what they allow", () => {
  const allowed = 1e6 / TIC_US;
  assert.equal(judgeTicOverload("x", { took: true, frameP99: 1.6, backlogMax: 2, ticsPerSecond: allowed }).failed, 0);
});

test("a frame waiting on owed tics, input running ahead or the wrong tic rate each fail it", () => {
  const ok = { took: true, frameP99: 1.6, backlogMax: 2, ticsPerSecond: 25 };
  assert.match(judgeTicOverload("x", { ...ok, frameP99: 41 }).line, /^FAIL x: frames p99 41\.0 ms/);
  // [rc4l] A background window paced at 60 Hz is not a frame waiting on a tic.
  assert.equal(judgeTicOverload("x", { ...ok, frameP99: 16.7 }).failed, 0);
  assert.equal(FRAME_CEILING_MS, 30);
  assert.match(judgeTicOverload("x", { ...ok, backlogMax: 34 }).line, /34 tics owed at once/);
  assert.match(judgeTicOverload("x", { ...ok, ticsPerSecond: 35 }).line, /35\.0 tics a second where 40 ms tics allow 25/);
  assert.match(judgeTicOverload("x", { ...ok, backlogMax: NaN }).line, /^FAIL/);
  assert.match(judgeTicOverload("x", { ...ok, took: false }).line, /did not take/);
});

test("statOf reads one counter's field and NaN for a counter the binary lacks", () => {
  const rows = [{ name: "frame/present_ms", p99: 2.5 }];
  assert.equal(statOf(rows, "frame/present_ms", "p99"), 2.5);
  assert.ok(Number.isNaN(statOf(rows, "handoff/backlog_tics", "max")));
});
