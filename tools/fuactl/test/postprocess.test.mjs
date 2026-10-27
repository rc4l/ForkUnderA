import { test } from "node:test";
import assert from "node:assert/strict";
import { buildPostProcessPk3, GLDEFS, SHADERS, STEPS, BOXES } from "../scenarios/postprocess.mjs";

// [Doruk-Aksoy] Every block names a shader the pk3 carries, and every step turns on one GLDEFS declares.
test("the post-process pk3 carries each shader its GLDEFS names", () => {
  assert.ok(buildPostProcessPk3().length > 0);
  const lumps = [...GLDEFS.matchAll(/Shader "shaders\/pp\/(\w+)\.fp" 330/g)].map((m) => m[1]);
  assert.ok(lumps.length >= 8);
  for (const lump of lumps) assert.ok(SHADERS[lump], `no shader ${lump}`);
  const names = new Set([...GLDEFS.matchAll(/Name "(\w+)"/g)].map((m) => m[1]));
  for (const step of STEPS)
    for (const c of step.commands)
      if (typeof c === "string" && c.startsWith("shaderenable")) assert.ok(names.has(c.split(" ")[1]), c);
});

// [Doruk-Aksoy] Each box compares two steps that exist.
test("every box judges two of the steps", () => {
  const steps = new Set(STEPS.map((s) => s.name));
  for (const [name, b] of Object.entries(BOXES.MAP01)) for (const p of b.pair) assert.ok(steps.has(p), `${name}: ${p}`);
});
