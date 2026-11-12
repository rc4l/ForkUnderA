import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeReadings, pushScript, buildNetMovePrecisionPk3, FLOOR, HOVER, PUSH, TOLERANCE } from "../scenarios/netmoveprecision.mjs";
import { ACSF, PCD } from "../src/acs.mjs";

test("the observer passes holding the server's speed and height, and names what it rounded", () => {
  const server = { vx: 0.5, z: 40.5 };
  assert.deepEqual(judgeReadings(server, { vx: 0.5, z: 40.5 }), []);
  assert.deepEqual(judgeReadings(server, { vx: 0, z: 40 }), ["the observer has the player moving at 0, the server 0.5", "the observer has the player at height 40, the server 40.5"]);
  assert.deepEqual(judgeReadings({ vx: 0, z: 40.5 }, { vx: 0, z: 40.5 }), ["the server's player moves at 0, not pushed"]);
  assert.deepEqual(judgeReadings(server, null), ["the pushed player is missing on the observer"]);
  assert.ok(TOLERANCE < PUSH && TOLERANCE < HOVER % 1);
});

test("the script holds the player at the fractional height and pushes them each tic, looping to its start", () => {
  const code = pushScript();
  assert.ok(code.includes((FLOOR + HOVER) * 65536) && code.includes(PUSH * 65536));
  assert.ok(code.includes(PCD.SETACTORPOSITION) && code.includes(ACSF.SetActorVelocity));
  assert.deepEqual(code.slice(-2), [PCD.GOTO, 8]);
  assert.ok(buildNetMovePrecisionPk3().toString("latin1").includes("maps/MAP01.wad"));
});
