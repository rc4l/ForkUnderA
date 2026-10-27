import { test } from "node:test";
import assert from "node:assert/strict";
import { buildScene16Pk3, DECORATE, GLDEFS, SHADERS, STEPS, BOXES } from "../scenarios/scene16.mjs";

// [Doruk-Aksoy] The pk3 carries both sprites' actors and each shader its GLDEFS names, and every box compares two steps.
test("the 16-bit scene pk3 carries its actors, shaders and checks", () => {
  const file = buildScene16Pk3();
  assert.ok(file.includes(Buffer.from("decorate.txt")));
  assert.match(DECORATE, /RenderStyle Add/);
  assert.match(DECORATE, /RenderStyle Subtract/);
  for (const [, lump] of GLDEFS.matchAll(/Shader "shaders\/s16\/(\w+)\.fp" 330/g)) assert.ok(SHADERS[lump], `no shader ${lump}`);
  const steps = new Set(STEPS.map((s) => s.name));
  for (const [name, b] of Object.entries(BOXES.MAP01)) for (const p of b.pair) assert.ok(steps.has(p), `${name}: ${p}`);
});
