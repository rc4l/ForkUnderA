import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeRailDecals, buildRailForceDecalPk3, DECORATE } from "../scenarios/railforcedecal.mjs";

test("the forced puff's rail adds one wall decal and the plain one's none", () => {
  assert.deepEqual(judgeRailDecals({ before: 0, forced: 1, plain: 1 }), []);
  assert.deepEqual(judgeRailDecals({ before: 0, forced: 0, plain: 0 }), ["the FORCEDECAL puff's rail left 0 wall decals, not 1"]);
  assert.deepEqual(judgeRailDecals({ before: 2, forced: 3, plain: 4 }), ["the plain puff's rail left 1 wall decals, where its weapon has none"]);
  assert.match(judgeRailDecals({ before: 0, forced: null, plain: 1 })[0], /no live count/);
});

test("only the forced puff carries FORCEDECAL, and each rail fires its own puff", () => {
  assert.equal(DECORATE.match(/\+FORCEDECAL/g).length, 1);
  assert.match(DECORATE, /actor ZXRailForce[\s\S]*"ZXForcePuff"/);
  assert.match(DECORATE, /actor ZXRailPlain[\s\S]*"ZXPlainPuff"/);
  assert.ok(buildRailForceDecalPk3().toString("latin1").includes("decorate.txt"));
});
