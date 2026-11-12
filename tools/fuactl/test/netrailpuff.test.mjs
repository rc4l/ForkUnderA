import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeRailPuffs, buildNetRailPuffPk3, TURRETS, DECORATE } from "../scenarios/netrailpuff.mjs";

const counts = (h, f, w) => ({ ZXStayPuffH: h, ZXStayPuffF: f, ZXStayPuffW: w });

test("no machine keeps a puff the sky took, both keep the plain wall's, and the client's rail marks once", () => {
  assert.deepEqual(judgeRailPuffs({ server: counts(0, 0, 3), observer: counts(0, 0, 3), decalsBefore: 0, decalsAfter: 1 }), []);
  assert.deepEqual(judgeRailPuffs({ server: counts(0, 0, 3), observer: counts(3, 3, 3), decalsBefore: 0, decalsAfter: 0 }), [
    "the observer kept 3 puffs the server took from the horizon", "the observer kept 3 puffs the server took from the sky floor",
    "the client's own rail left 0 wall decals, not its FORCEDECAL puff's 1"]);
  assert.deepEqual(judgeRailPuffs({ server: counts(0, 0, 3), observer: {}, decalsBefore: null, decalsAfter: 1 }), [
    "the plain wall's puffs are 3 on the server and 0 on the observer, where both must have some", "the shooting client's fua_decal_report gave no live count"]);
});

test("each turret fires its own puff, the sky floor's aimed down, and every turret is placed", () => {
  for (const t of TURRETS) assert.ok(DECORATE.includes(`"${t.puff}", 0, ${t.spreadZ})`), t.puff);
  assert.deepEqual(TURRETS.map((t) => t.spreadZ !== 0), [false, true, false]);
  assert.equal(new Set(TURRETS.map((t) => t.ednum)).size, TURRETS.length);
  assert.ok(buildNetRailPuffPk3().toString("latin1").includes("decorate.txt"));
});
