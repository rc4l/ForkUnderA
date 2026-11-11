// [rc4l] One room, the player looking down at its floor: exit 0 when the level mesh still holds the floor after `gl_wallmesh 0` and `gl_wallmesh 1` are typed, where that setting's toggle dropped every floor the map bake had made and players showed through them, and after fua_mesh_rebuild bakes it again.
//
// usage:  node scenarios/meshfloors.mjs
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 200 };
export const TOGGLE = ["gl_wallmesh 0", "gl_wallmesh 1"];

// [rc4l] A room with the player at its centre.
export function buildMeshFloorsWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 256, angle: 0, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Whether fua_look found a mesh piece along the crosshair, or null when it printed neither answer.
export function lookHit(text) {
  if (/mesh: piece \d+/.test(text)) return true;
  return /mesh: NOTHING along that ray/.test(text) ? false : null;
}

// [rc4l] How many planes fua_mesh_rebuild said it baked, or -1 when it said nothing of them.
export function rebuiltPlanes(text) {
  const m = /fua_mesh_rebuild: \d+ wall parts and (\d+) planes/.exec(String(text ?? ""));
  return m ? Number(m[1]) : -1;
}

// [rc4l] The verdict from fua_look at the floor before and after the toggle was typed, and after the rebuild.
export function judgeMeshFloors(label, { before, after, planes, rebuilt }) {
  if (before !== true) return { line: `FAIL ${label}: the mesh held no floor under the player before the toggle (${before === false ? "nothing" : "no answer"})`, failed: 1 };
  if (after !== true) return { line: `FAIL ${label}: after ${TOGGLE.join(" and ")}, the mesh held no floor under the player`, failed: 1 };
  if (planes <= 0 || rebuilt !== true) return { line: `FAIL ${label}: fua_mesh_rebuild baked ${planes < 0 ? "no" : planes} planes and the floor ${rebuilt ? "stayed" : "went"}`, failed: 1 };
  return { line: `PASS ${label}: the floor stayed in the level mesh through ${TOGGLE.join(" and ")}, and fua_mesh_rebuild baked ${planes} planes`, failed: 0 };
}

export async function judgeLaunch(c, inst, label) {
  await cap.sandbox(c);
  const cam = await c.rpc("player.camera");
  // [rc4l] Positive pitch looks down.
  await c.rpc("player.setpos", { x: cam.x, y: cam.y, z: cam.z, angle: 0, pitch: 80 });
  await cap.waitTics(c, 4);
  const before = lookHit(await cap.exec(c, "fua_look", { quietMs: 400 }));
  for (const t of TOGGLE) await cap.exec(c, t);
  await cap.waitTics(c, 20);
  const after = lookHit(await cap.exec(c, "fua_look", { quietMs: 400 }));
  const planes = rebuiltPlanes(await cap.exec(c, "fua_mesh_rebuild", { quietMs: 400 }));
  await cap.waitTics(c, 8);
  const rebuilt = lookHit(await cap.exec(c, "fua_look", { quietMs: 400 }));
  return judgeMeshFloors(label, { before, after, planes, rebuilt });
}

runScenarioIfMain(import.meta.url, () => ({ name: "meshfloors", wad: buildMeshFloorsWad(), maps: ["MAP01"], sides: [{ name: "vk" }], judgeLaunch }), compareEachLaunch);
