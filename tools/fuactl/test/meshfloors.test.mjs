import { test } from "node:test";
import assert from "node:assert/strict";
import { readWad } from "../src/udmf.mjs";
import { buildMeshFloorsWad, lookHit, rebuiltPlanes, judgeMeshFloors } from "../scenarios/meshfloors.mjs";

test("meshfloors builds one room with the player in it", () => {
  const lumps = readWad(buildMeshFloorsWad());
  assert.deepEqual(lumps.map((l) => l.name), ["MAP01", "TEXTMAP", "ENDMAP"]);
  assert.match(lumps[1].data.toString("latin1"), /type = 1;/);
});

test("meshfloors reads fua_look's answer and fails a floor the toggle dropped", () => {
  assert.equal(lookHit("fua_look: from (0, 0, 41)\n  mesh: piece 12 at 41 away, range 3+6\n"), true);
  assert.equal(lookHit("  mesh: NOTHING along that ray -- the surface is not baked.\n"), false);
  assert.equal(lookHit("Unknown command"), null);
  assert.equal(rebuiltPlanes("fua_mesh_rebuild: 4 wall parts and 2 planes built from the map\n"), 2);
  assert.equal(rebuiltPlanes("Unknown command \"fua_mesh_rebuild\""), -1);
  const good = { before: true, after: true, planes: 2, rebuilt: true };
  assert.equal(judgeMeshFloors("MAP01 vk", good).failed, 0);
  assert.match(judgeMeshFloors("MAP01 vk", { ...good, after: false }).line, /^FAIL .*after gl_wallmesh 0 and gl_wallmesh 1, the mesh held no floor/);
  assert.match(judgeMeshFloors("MAP01 vk", { ...good, before: null }).line, /before the toggle \(no answer\)/);
  assert.match(judgeMeshFloors("MAP01 vk", { ...good, before: false }).line, /before the toggle \(nothing\)/);
  assert.match(judgeMeshFloors("MAP01 vk", { ...good, planes: -1 }).line, /fua_mesh_rebuild baked no planes and the floor stayed/);
  assert.match(judgeMeshFloors("MAP01 vk", { ...good, rebuilt: false }).line, /baked 2 planes and the floor went/);
});
