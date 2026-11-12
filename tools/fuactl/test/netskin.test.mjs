import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeSprite, wantedSprite, buildNetSkinPk3, buildClassSkinWad, CONFIGS, CLASS_S_SKIN, KEYCONF, CLASS_SKIN_SPRITE, SKIN_SPRITE } from "../scenarios/netskin.mjs";

test("each configuration wants the sprite its skinned player should show", () => {
  assert.deepEqual(CONFIGS.map(wantedSprite), [SKIN_SPRITE, "PLAY", CLASS_SKIN_SPRITE]);
});

test("a body in the wanted sprite passes, another or none is named", () => {
  const actors = [{ player: 0, sprite: "ZXTWA" }, { player: 1, sprite: "PLAYA" }];
  assert.deepEqual(judgeSprite(actors, 0, CLASS_SKIN_SPRITE), []);
  assert.deepEqual(judgeSprite(actors, 1, CLASS_SKIN_SPRITE), ["shows player 1 as PLAYA, not ZXTW"]);
  assert.deepEqual(judgeSprite(actors, 2, "PLAY"), ["holds no body for player 2"]);
});

test("the class skin belongs to the second class, which the pk3 adds", () => {
  assert.match(CLASS_S_SKIN, /class = ZxTwo/);
  assert.match(KEYCONF, /^clearplayerclasses\naddplayerclass ZxOne\naddplayerclass ZxTwo\n$/);
  const skinWad = buildClassSkinWad().toString("latin1");
  for (const name of ["S_SKIN", "ZXTWA0", "ZXTWB0"]) assert.ok(skinWad.includes(name), name);
  const bytes = buildNetSkinPk3().toString("latin1");
  for (const name of ["zxtwoskin.wad", "keyconf.txt", "decorate.txt", "sprites/ZXSKB0.png"]) assert.ok(bytes.includes(name), name);
});
