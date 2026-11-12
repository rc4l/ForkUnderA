import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeMorph, buildMorphSkinPk3, DECORATE, MORPH } from "../scenarios/morphskin.mjs";

test("a morph in its own sprite passes, in a skin's or as another class is named", () => {
  assert.deepEqual(judgeMorph([{ player: 0, c: MORPH, sprite: "TROOA" }], 0), []);
  assert.deepEqual(judgeMorph([{ player: 0, c: MORPH, sprite: "PLAYA" }], 0), ["shows the morphed player 0 as PLAYA, not TROO"]);
  assert.deepEqual(judgeMorph([{ player: 0, c: "DoomPlayer", sprite: "PLAYA" }], 0), ["shows player 0 as DoomPlayer, not ZxMorph"]);
  assert.deepEqual(judgeMorph([], 0), ["holds no body for player 0"]);
});

test("the morph class has no NOSKIN and the pk3 holds it", () => {
  assert.doesNotMatch(DECORATE, /NOSKIN/i);
  assert.ok(buildMorphSkinPk3().toString("latin1").includes("decorate.txt"));
});
