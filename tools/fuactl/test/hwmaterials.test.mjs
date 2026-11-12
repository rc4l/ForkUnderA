// [Doruk-Aksoy] The Material template: every map and map texture its GLDEFS names is in the pk3, and each check pairs two of its maps in both optimizer runs.
import test from "node:test";
import assert from "node:assert/strict";
import { readPk3 } from "../src/udmf.mjs";
import { buildHwMaterialsPk3, GLDEFS, CASES, SIDES, BOXES } from "../scenarios/hwmaterials.mjs";

test("the Material pk3 holds a map per case and every texture GLDEFS names", () => {
  const names = readPk3(buildHwMaterialsPk3()).map((f) => f.name);
  CASES.forEach((_, i) => assert.ok(names.includes(`maps/MAP${String(i + 1).padStart(2, "0")}.wad`)));
  for (const [, t] of GLDEFS.matchAll(/\b(ZX[A-Z]+)\b/g)) assert.ok(names.includes(`textures/${t}.png`), t);
  for (const [, s] of GLDEFS.matchAll(/"(shaders\/\w+\.fp)"/g)) assert.ok(names.includes(s), s);
});

test("each check pairs two sides of one optimizer run", () => {
  const sides = new Set(SIDES.map((s) => s.name));
  assert.equal(sides.size, CASES.length * 2);
  for (const [name, check] of Object.entries(BOXES.MAP01)) {
    for (const side of check.pair) assert.ok(sides.has(side), `${name} ${side}`);
    assert.equal(check.pair[0].replace(/\d+$/, ""), check.pair[1].replace(/\d+$/, ""), name);
    assert.ok((check.floor ?? 0) > 0 || (check.ceiling ?? 0) > 0, name);
  }
});
