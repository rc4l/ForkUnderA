import { test } from "node:test";
import assert from "node:assert/strict";
import { smoothTics, judgeSmooth, MIN_SMOOTH_TICS, buildViewTurnPk3 } from "../scenarios/viewturn.mjs";

const frames = (perTic) => perTic.flatMap((angles, i) => angles.map((yaw, k) => ({ gametic: 100 + i, frac: k / angles.length, yaw, pitch: 0 })));

test("a tic counts as smooth when its frames drew more than one angle", () => {
  assert.equal(smoothTics(frames([[0, 10, 20], [20, 20], [40, 50]]), "yaw"), 2);
  assert.equal(smoothTics(frames([[0, 0], [20, 20]]), "yaw"), 0);
});

test("a stepped turn is named, a smooth one passes, a still one is named", () => {
  const smooth = frames([[0, 10], [20, 30], [40, 50], [60, 70]]);
  const stepped = frames([[0, 0], [20, 20], [40, 40], [60, 60]]);
  assert.deepEqual(judgeSmooth("the turn", smooth, "yaw"), []);
  assert.deepEqual(judgeSmooth("the turn", stepped, "yaw"), [`the turn drew between tics in 0 tics, not ${MIN_SMOOTH_TICS}`]);
  assert.deepEqual(judgeSmooth("the turn", frames([[5, 5], [5, 5]]), "yaw"), ["the turn moved the view 0.00 degrees"]);
  assert.deepEqual(judgeSmooth("the turn", [], "yaw"), ["the turn drew 0 frames"]);
});

test("the pk3 holds the room", () => {
  assert.ok(buildViewTurnPk3().toString("latin1").includes("maps/MAP01.wad"));
});
