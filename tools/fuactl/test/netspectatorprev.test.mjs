import { test } from "node:test";
import assert from "node:assert/strict";
import { judgePrev, buildNetSpectatorPrevPk3, STOPPED_LAG } from "../scenarios/netspectatorprev.mjs";

const START = { x: 128, y: 128, px: 128, py: 128 };

test("a stopped spectator's previous position must have kept up with its flight", () => {
  assert.deepEqual(judgePrev(START, { x: 900, y: 128, px: 900, py: 128 }), []);
  assert.deepEqual(judgePrev(START, { x: 900, y: 128, px: 900 - STOPPED_LAG, py: 128 }), []);
  assert.deepEqual(judgePrev(START, { x: 900, y: 128, px: 128, py: 128 }), ["the stopped spectator's previous position is 772.0 units behind it, after a flight of 772.0"]);
});

test("a spectator that never flew or has no pawn is a fault of the run, not a pass", () => {
  assert.deepEqual(judgePrev(START, { x: 140, y: 128, px: 140, py: 128 }), ["the spectator flew only 12.0 units"]);
  assert.deepEqual(judgePrev(null, START), ["the spectating client has no pawn of its own"]);
  assert.deepEqual(judgePrev(START, { x: 900, y: 128 }), ["this build's world.actors reports no previous position"]);
  assert.ok(buildNetSpectatorPrevPk3().toString("latin1").includes("maps/MAP01.wad"));
});
