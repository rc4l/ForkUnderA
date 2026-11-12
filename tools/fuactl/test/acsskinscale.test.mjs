import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeDrawnScale, buildScript, SET_PLAYER_SKIN, SKIN_SCALE } from "../scenarios/acsskinscale.mjs";
import { PCD } from "../src/acs.mjs";

const pawn = (dsx, dsy = dsx) => ({ c: "DoomPlayer", player: 0, dsx, dsy });

test("player 0's body is judged by the scale it is drawn at", () => {
  assert.deepEqual(judgeDrawnScale([pawn(SKIN_SCALE)], SKIN_SCALE), []);
  assert.deepEqual(judgeDrawnScale([pawn(1)], 1), []);
  assert.deepEqual(judgeDrawnScale([pawn(1)], SKIN_SCALE), [`player 0 is drawn at 1 by 1, not ${SKIN_SCALE}`]);
  assert.deepEqual(judgeDrawnScale([{ c: "DoomPlayer", player: -1, dsx: 2, dsy: 2 }], 2), ["player 0 has no body"]);
  assert.deepEqual(judgeDrawnScale(undefined, 1), ["player 0 has no body"]);
});

test("the script gives player 0 the skin through SetPlayerSkin", () => {
  assert.deepEqual(buildScript().slice(4, 7), [PCD.CALLFUNC, 2, SET_PLAYER_SKIN]);
});
