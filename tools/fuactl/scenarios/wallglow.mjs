// [rc4l] A dark room whose floor glows red and ceiling blue by GLDEFS, GL against Vulkan: exit 0 when Vulkan lights the walls from the flats, fading over each glow's height, as GL's glowing walls do.
//
// usage:  node scenarios/wallglow.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

export function buildWallGlowWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 64 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  return wad([...mapLumps("MAP01", m),
    { name: "GLDEFS", data: `glow\n{\n  Texture "FLOOR4_8", ff2020, 96\n  Texture "CEIL3_5", 2040ff, 48\n}\n` }]);
}

export const VANTAGES = [{ name: "wall", at: { x: 256, y: 64, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The far wall from floor to ceiling, where both glows fall.
export const BOXES = { MAP01: { wall: { box: [0.25, 0.2, 0.75, 0.7], ceiling: 3 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "wallglow", wad: buildWallGlowWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
