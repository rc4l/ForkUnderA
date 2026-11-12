// [Doruk-Aksoy] The texture shader uniforms template: its pk3 carries every lump GLDEFS and the scripts name, the jumping script comes first, and each check pairs a step with its oracle.
import test from "node:test";
import assert from "node:assert/strict";
import { readPk3 } from "../src/udmf.mjs";
import { buildHwShaderUniformsPk3, GLDEFS, SHADERS, SCRIPTS, STRINGS, ORACLES, STEPS, BOXES } from "../scenarios/hwshaderuniforms.mjs";

test("the uniforms pk3 holds each shader, texture and oracle it names", () => {
  const names = readPk3(buildHwShaderUniformsPk3()).map((f) => f.name);
  for (const s of Object.keys(SHADERS)) assert.ok(names.includes(`shaders/${s}.fp`), s);
  for (const t of ["ZXUA", "ZXUB", "ZXUC", "ZXUD", ...ORACLES.defaults.pieces, ...ORACLES.set.pieces, "ZXWHITE"]) assert.ok(names.includes(`textures/${t}.png`), t);
  for (const g of ["ZXHUDU", ORACLES.defaults.hud, ORACLES.set.hud]) assert.ok(names.includes(`graphics/${g}.png`), g);
  assert.ok(names.includes("maps/MAP01.wad"));
});

test("every texture shader block names a set, and one shares another's", () => {
  const blocks = GLDEFS.split("\n").filter((l) => l.startsWith("HardwareShader"));
  assert.equal(blocks.length, 5);
  for (const b of blocks) assert.match(b, /fua_Name "zx\w+"/);
  assert.equal(blocks.filter((b) => b.includes('fua_Name "zxtint"')).length, 2);
  assert.match(SHADERS.tint, /^uniform vec3 tint;/);
});

test("the script with jumps is the lump's first, and every string a script names is in the table", () => {
  assert.equal(SCRIPTS[0].number, 5);
  assert.equal(new Set(STRINGS).size, STRINGS.length);
  for (const t of [...ORACLES.defaults.pieces, ...ORACLES.set.pieces, ORACLES.defaults.hud, ORACLES.set.hud]) assert.ok(STRINGS.includes(t), t);
});

test("each check pairs a step with the oracle step before it", () => {
  const steps = STEPS.map((s) => s.name);
  for (const [name, check] of Object.entries(BOXES.MAP01)) {
    const [ref, step] = check.pair;
    assert.ok(ref.startsWith("ref"), name);
    assert.ok(steps.indexOf(ref) < steps.indexOf(step), name);
  }
});
