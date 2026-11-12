import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeLights, buildResetLightListPk3, LOWER_SCRIPT, LOWER, MAP_LIGHTS } from "../scenarios/resetlightlist.mjs";

test("a light list passes at the heights wanted, within half a unit", () => {
  assert.deepEqual(judgeLights({ lights: [256, 96.2] }, MAP_LIGHTS, "after the reset"), []);
  assert.deepEqual(judgeLights({ lights: [192, 80] }, MAP_LIGHTS, "after the reset"), ["after the reset the room's light list is 192,80, not 256,96"]);
  assert.deepEqual(judgeLights({ lights: [256] }, MAP_LIGHTS, "after the reset"), ["after the reset the room's light list is 256, not 256,96"]);
  assert.deepEqual(judgeLights(undefined, MAP_LIGHTS, "lowered,"), ["lowered, the room's light list is empty, not 256,96"]);
});

test("the pk3 holds the map and the script lowers both ceilings", () => {
  assert.ok(buildResetLightListPk3().toString("latin1").includes("maps/MAP01.wad"));
  for (const by of LOWER) assert.ok(LOWER_SCRIPT.includes(by), by);
});
