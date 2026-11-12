import { test } from "node:test";
import assert from "node:assert/strict";
import { judgePitches, LOOK_DOWN, CONFIGS } from "../scenarios/netcenterview.mjs";

test("a look down then a level view passes", () => {
  assert.deepEqual(judgePitches(LOOK_DOWN, 0), []);
  assert.deepEqual(judgePitches(LOOK_DOWN, -0.5), []);
});

test("a centering stopped short, or a look that never went down, fails", () => {
  assert.deepEqual(judgePitches(LOOK_DOWN, 3.95), ["still 3.95 degrees off level after centerview"]);
  assert.deepEqual(judgePitches(0, 0), ["looked down only 0.00 degrees before centerview"]);
  assert.deepEqual(judgePitches(undefined, 0), ["looked down only undefined degrees before centerview"]);
});

test("every configuration lags, and one loses packets too", () => {
  assert.ok(CONFIGS.every((c) => Number(c.lag) > 0));
  assert.ok(CONFIGS.some((c) => Number(c.loss) > 0));
});
