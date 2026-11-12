import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeServerRoom, buildNetLightListPk3, LOWER_SCRIPT, LOWER_BY } from "../scenarios/netlightlist.mjs";

test("the server's room passes lowered with its light list starting at the new ceiling", () => {
  const want = 256 - LOWER_BY;
  assert.deepEqual(judgeServerRoom({ ceiling: want, lights: [want, 64] }), []);
  assert.deepEqual(judgeServerRoom({ ceiling: 256, lights: [256, 64] }), [`the server's room ceiling is 256, not ${want}`, `the server's light list starts at 256, not its ceiling's ${want}`]);
  assert.deepEqual(judgeServerRoom({ ceiling: want, lights: [want] }), [`the server's room has light list ${want}, not one under a 3D floor`]);
  assert.deepEqual(judgeServerRoom(undefined), ["the server reported no room sector"]);
});

test("the pk3 holds the map and the script lowers by a byte value", () => {
  assert.ok(buildNetLightListPk3().toString("latin1").includes("maps/MAP01.wad"));
  assert.ok(LOWER_SCRIPT.includes(LOWER_BY));
});
