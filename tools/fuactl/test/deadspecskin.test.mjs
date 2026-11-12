import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeBody, buildScript, SKIN_SCALE, SET_DEAD_SPECTATOR, SIDES } from "../scenarios/deadspecskin.mjs";
import { PCD } from "../src/acs.mjs";

const body = (sx, sy = sx) => ({ c: "DoomPlayer", player: -1, sx, sy });

test("the body left behind keeps the skin's scale once", () => {
  assert.deepEqual(judgeBody([body(SKIN_SCALE), { c: "DoomPlayer", player: 0, sx: 1, sy: 1 }]), []);
  assert.deepEqual(judgeBody([body(4)]), ["the body is scaled 4 by 4, not the skin's 2"]);
  assert.deepEqual(judgeBody([]), ["left 0 bodies, not one"]);
  assert.deepEqual(judgeBody([body(2), body(2)]), ["left 2 bodies, not one"]);
});

test("the script turns player 0 into a dead spectator, in survival", () => {
  assert.deepEqual(buildScript().slice(4, 7), [PCD.CALLFUNC, 2, SET_DEAD_SPECTATOR]);
  assert.equal(SIDES[0].launchCvars.survival, 1);
});
