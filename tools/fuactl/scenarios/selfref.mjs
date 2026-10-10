// [rc4l] A sector hidden inside a room by self-referencing lines, GL against Vulkan: exit 0 when Vulkan draws the room's floor across it, as GL does with the subsector's render sector.
//
// usage:  node scenarios/selfref.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

export function buildSelfRefWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  // [rc4l] Raised and blue, so a bake that draws the nodebuilder's sector shows a floating patch where GL shows the room's floor.
  m.selfRef(160, 224, 352, 416, { heightfloor: 48, heightceiling: 256, texturefloor: "FLAT14", textureceiling: "CEIL3_5",
    lightlevel: 255 });
  m.thing({ x: 256, y: 96, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "floor", at: { x: 256, y: 96, z: 0, angle: 90, pitch: 25 } }];

// [rc4l] Where the hidden square lands from this vantage: 12.35 when the bake drew the nodebuilder's sector, 0 with the render sector.
export const BOXES = { MAP01: { floor: { box: [0.25, 0.25, 0.75, 0.42], ceiling: 3 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "selfref", wad: buildSelfRefWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
