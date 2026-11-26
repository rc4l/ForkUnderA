import { test } from "node:test";
import assert from "node:assert/strict";
import { parseAsks, judgeAsks, readAsks } from "../src/asks.mjs";

test("the ask count is read from fua_gamethread_report's line", () => {
  assert.equal(parseAsks("game thread: 120 tics on its own thread, 3 frames held, 17 calls asked of the main thread\n"), 17);
  assert.equal(parseAsks("unknown command"), null);
});

test("a stretch that asks at most the most it may passes", () => {
  assert.deepEqual(judgeAsks(17, 19, 2), []);
  assert.deepEqual(judgeAsks(5, 5, 0), []);
});

test("a stretch that asks more fails, with the count", () => {
  const faults = judgeAsks(10, 40, 2);
  assert.equal(faults.length, 1);
  assert.match(faults[0], /asked the main thread 30 times, more than 2/);
});

test("a missing count fails", () => {
  assert.deepEqual(judgeAsks(null, 3, 2), ["fua_gamethread_report printed no ask count"]);
  assert.deepEqual(judgeAsks(3, null, 2), ["fua_gamethread_report printed no ask count"]);
});

test("readAsks runs the report on the launch and reads its count", async () => {
  const seen = [];
  const cap = { exec: async (c, cmd) => { seen.push([c, cmd]); return "game thread: 1 tics on its own thread, 0 frames held, 9 calls asked of the main thread"; } };
  assert.equal(await readAsks(cap, "conn"), 9);
  assert.deepEqual(seen, [["conn", "fua_gamethread_report"]]);
});
