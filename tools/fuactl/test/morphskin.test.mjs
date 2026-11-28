import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeMorph, judgeBody, buildMorphSkinPk3, DECORATE, MORPH, BASE } from "../scenarios/morphskin.mjs";

test("a morph in its own sprite passes, in a skin's or as another class is named", () => {
  assert.deepEqual(judgeMorph([{ player: 0, c: MORPH, sprite: "TROOA" }], 0), []);
  assert.deepEqual(judgeMorph([{ player: 0, c: MORPH, sprite: "PLAYA" }], 0), ["shows player 0 as PLAYA, not TROO"]);
  assert.deepEqual(judgeMorph([{ player: 0, c: "DoomPlayer", sprite: "PLAYA" }], 0), ["shows player 0 as DoomPlayer, not ZxMorph"]);
  assert.deepEqual(judgeMorph([], 0), ["holds no body for player 0"]);
});

test("an unmorphed player passes as its own class, and two bodies for one player are named", () => {
  assert.deepEqual(judgeBody([{ player: 0, c: BASE, sprite: "PLAYA" }], 0, BASE, "PLAY"), []);
  assert.deepEqual(judgeBody([{ player: 0, c: BASE, sprite: "PLAYA" }, { player: 0, c: MORPH, sprite: "TROOA" }], 0, BASE, "PLAY"), ["holds 2 bodies for player 0: DoomPlayer, ZxMorph"]);
  assert.deepEqual(judgeBody([{ player: 1, c: BASE, sprite: "PLAYA" }], 0, BASE, "PLAY"), ["holds no body for player 0"]);
});

test("the morph class has no NOSKIN and the pk3 holds it", () => {
  assert.doesNotMatch(DECORATE, /NOSKIN/i);
  assert.ok(buildMorphSkinPk3().toString("latin1").includes("decorate.txt"));
});
