import { test } from "node:test";
import assert from "node:assert/strict";
import { readWad } from "../src/udmf.mjs";
import { buildHitboxOneTicWad, magentaPixels, judgeHitboxOneTic, OVERLAY, MIN_DRAWN, MAX_HIDDEN } from "../scenarios/hitboxonetic.mjs";

test("the wad holds the map and a gun whose Ready spawns a one-tic projectile box ahead", () => {
  const lumps = readWad(buildHitboxOneTicWad());
  assert.deepEqual(lumps.map((l) => l.name).slice(0, 2), ["MAP01", "TEXTMAP"]);
  const decorate = lumps.find((l) => l.name === "DECORATE").data.toString("latin1");
  assert.match(decorate, /actor ZXOneTicBox\s*\{\s*PROJECTILE/);
  assert.match(decorate, /Spawn:\s+TNT1 A 1\s+Stop/);
  assert.match(decorate, /Ready:\s+PISG A 0 A_SpawnItemEx\("ZXOneTicBox", 160/);
});

test("the overlay settings hide the player's box and colour the boxes magenta", () => {
  assert.ok(OVERLAY.includes("cl_fua_hitbox_self 0"));
  assert.ok(OVERLAY.some((l) => /cl_fua_hitbox_color "ff 00 ff"/.test(l)));
});

test("magentaPixels counts only near-magenta pixels", () => {
  const img = { w: 3, h: 1, channels: 3, colorType: 2, data: Buffer.from([255, 0, 255, 220, 40, 210, 255, 100, 255]) };
  assert.equal(magentaPixels(img), 2);
});

test("the verdict passes a box drawn with the option on and hidden with it off", () => {
  assert.match(judgeHitboxOneTic("MAP01 vk", { on: 0, off: 0 }).line, /^FAIL MAP01 vk: the one-tic box is not drawn/);
  assert.equal(judgeHitboxOneTic("MAP01 vk", { on: MIN_DRAWN - 1, off: 0 }).failed, 1);
  assert.match(judgeHitboxOneTic("MAP01 vk", { on: 900, off: MAX_HIDDEN + 1 }).line, /still drawn with cl_fua_hitbox_onetic 0/);
  assert.deepEqual(judgeHitboxOneTic("MAP01 gl", { on: 900, off: 0 }),
    { line: "PASS MAP01 gl: the one-tic box drawn (900 magenta pixels), and hidden with the option off (0)", failed: 0 });
});
