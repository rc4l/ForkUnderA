// [rc4l] GL alone against GL in a Vulkan session whose level mesh has been emptied: exit 0 when GL still draws its walls, because it draws them from its own vertices.
//
// usage:  node scenarios/glplain.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] Queue #42: a Vulkan session switched the level mesh on and GL drew its walls from its own uploaded copy, a millisecond a frame on AOW03.
export function buildGlPlainWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    (edge) => ({ side: { texturemiddle: edge === "north" ? "REDWALL" : "STARTAN2" } }));
  m.thing({ x: 256, y: 200, angle: 270, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Grey first, because GL never clears its frame: a wall it stops drawing keeps the last picture there, so the red wall must be turned to after the mesh is emptied.
export const VANTAGES = [
  { name: "away", at: { x: 256, y: 200, z: 0, angle: 270, pitch: 0 } },
  { name: "red", at: { x: 256, y: 200, z: 0, angle: 90, pitch: 0 } },
];

export const BOXES = { MAP01: { red: { box: [0.2, 0.2, 0.8, 0.8], ceiling: 4, vantage: "red" } } };

// [rc4l] The level reloads with the map bake off so GL's own capture fills the mesh, then the mesh is emptied while GL looks away; a GL that drew from it leaves the red wall grey.
export const SIDES = [{ name: "gl", gl: true },
  { name: "blanked", frame: "gl", commands: ["fua_surface_mapbake_auto 0", "map MAP01"], atVantage: ["fua_mesh_blank"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "glplain", wad: buildGlPlainWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
