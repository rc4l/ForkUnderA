// [Doruk-Aksoy] The actor material template: its pk3 carries what GLDEFS and DECORATE name, its script with jumps comes first, and each check pairs a step with an earlier one.
import test from "node:test";
import assert from "node:assert/strict";
import { readPk3 } from "../src/udmf.mjs";
import { buildHwActorMaterialsPk3, GLDEFS, SHADERS, SCRIPTS, STRINGS, STEPS, BOXES } from "../scenarios/hwactormaterials.mjs";

test("the actor material pk3 holds its map, DECORATE, sprite and every shader GLDEFS names", () => {
  const files = Object.fromEntries(readPk3(buildHwActorMaterialsPk3()).map((f) => [f.name, f.data.toString("latin1")]));
  for (const name of ["maps/MAP01.wad", "gldefs.txt", "decorate.txt", "sprites/ZXASA0.png"]) assert.ok(name in files, name);
  for (const [, lump] of GLDEFS.matchAll(/Shader "([^"]+)"/g)) assert.ok(lump in files, lump);
  assert.equal(Object.keys(SHADERS).length, [...GLDEFS.matchAll(/fua_Material/g)].length);
  assert.match(files["decorate.txt"], /fua_Material "zxglow"/);
});

test("the script with jumps is the lump's first and every name a script passes is in the table", () => {
  assert.equal(SCRIPTS[0].number, 5);
  assert.equal(new Set(STRINGS).size, STRINGS.length);
  for (const [, name] of GLDEFS.matchAll(/fua_Material "([^"]+)"/g)) assert.ok(STRINGS.includes(name), name);
});

test("each check compares two steps the run takes", () => {
  const order = STEPS.map((s) => s.name);
  for (const [name, check] of Object.entries(BOXES.MAP01)) {
    const [a, b] = check.pair;
    assert.ok(a !== b && order.includes(a) && order.includes(b), name);
  }
});
