// [rc4l] A point light on a far wall, Vulkan with its light grid against without, from several distances: exit 0 when every box is under its ceiling.
//
// usage:  node scenarios/pointlight.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] A dark hall 4096 deep with a tall north wall, and a white point light above eye level standing just off it.
export function buildPointlightWad() {
  const m = new UdmfMap();
  m.room(0, 0, 2048, 4096, { heightfloor: 0, heightceiling: 1024, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 32 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 1024, y: 64, angle: 90, type: 1 });
  m.thing({ x: 1024, y: 4032, height: 256, type: 9800, arg0: 255, arg1: 255, arg2: 255, arg3: 160 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] The camera walks back from the wall, so the light's footprint on screen crosses tile rows at each step.
export const VANTAGES = [600, 1000, 1500, 2200, 3000].map((d) => (
  { name: `d${d}`, at: { x: 1024, y: 4096 - d, z: 0, angle: 90, pitch: 0 } }));

// [rc4l] Everything below the console's notify lines: the culled light must match the unculled one, which measured 0.00 once fixed.
export const BOXES = { MAP01: { frame: { box: [0.0, 0.05, 1.0, 1.0], ceiling: 0.05 } } };

// [rc4l] Vulkan with the light grid against Vulkan without it, since the grid alone decides which tiles a light reaches.
export const SIDES = [{ name: "flat", cvars: { fua_dg_clusters: 0 } }, { name: "clustered", cvars: { fua_dg_clusters: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "pointlight", wad: buildPointlightWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
