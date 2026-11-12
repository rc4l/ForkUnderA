import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeSkyPuffs, buildRailSkyPuffPk3, DECORATE, SHOTS } from "../scenarios/railskypuff.mjs";

test("each shot passes when its puffs stay as many as it says", () => {
  const ran = SHOTS.map((s) => ({ ...s, before: 2, after: 2 + s.stays }));
  assert.deepEqual(judgeSkyPuffs(ran), []);
  const stayed = ran.map((r) => (r.name === "a horizon line" ? { ...r, after: r.before + 1 } : r));
  assert.deepEqual(judgeSkyPuffs(stayed), ["a rail into a horizon line left 1 of its puffs, not 0"]);
});

test("only the SKYEXPLODE puff explodes on sky, and the horizon shots share the horizon room", () => {
  assert.equal(DECORATE.match(/\+SKYEXPLODE/g).length, 1);
  assert.doesNotMatch(DECORATE, /RANDOMIZE/, "a randomized puff's endless state would end after a tic");
  assert.deepEqual(SHOTS.filter((s) => /horizon/.test(s.name)).map((s) => s.x0), [640, 640]);
  assert.ok(buildRailSkyPuffPk3().toString("latin1").includes("decorate.txt"));
});
