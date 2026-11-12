import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeMarks, buildNetRailDecalPk3, DECORATE, TURRETS } from "../scenarios/netraildecal.mjs";

test("the observer passes with wall decals and floor marks, and names whichever it lacks", () => {
  assert.deepEqual(judgeMarks({ walls: 3, flats: 3 }), []);
  assert.deepEqual(judgeMarks({ walls: 0, flats: 0 }), TURRETS.map((t) => `the observer holds no ${t.marks === "walls" ? "wall decals" : "floor marks"} from ${t.name}'s rails`));
  assert.deepEqual(judgeMarks({ walls: null, flats: null }), ["the observer's fua_decal_report gave no live count"]);
});

test("only the wall turret has a decal of its own, and the floor turret's puff is FORCEDECAL", () => {
  const block = (name) => DECORATE.slice(DECORATE.indexOf(`actor ${name}`)).split("\n}\n")[0];
  assert.match(block("ZXWallTurret"), /Decal BulletChip/);
  assert.doesNotMatch(block("ZXFloorTurret"), /Decal /);
  assert.match(block("ZXDecalPuff"), /\+FORCEDECAL/);
  assert.ok(buildNetRailDecalPk3().toString("latin1").includes("decorate.txt"));
});
