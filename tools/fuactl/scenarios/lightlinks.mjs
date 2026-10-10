// [rc4l] A point light behind a solid wall, GL against Vulkan: exit 0 when Vulkan leaves the near room dark, as GL's light links (which stop at solid walls) do.
//
// usage:  node scenarios/lightlinks.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { THING } from "../src/doomnums.mjs";

const ROOM = { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 128 };

export function buildLightLinksWad() {
  const m = new UdmfMap();
  m.room(0, 0, 256, 256, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  // [rc4l] A separate room 8 units east, closed on every side, so nothing joins the two.
  m.room(264, 0, 520, 256, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 64, y: 128, angle: 0, type: 1 });
  // [rc4l] 16 units past the wall with a 160 radius, so by distance alone it reaches most of the near room's floor.
  m.thing({ x: 280, y: 128, angle: 0, type: THING.PointLight, height: 48, arg0: 255, arg1: 0, arg2: 0, arg3: 160 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "near", at: { x: 64, y: 128, z: 0, angle: 0, pitch: 20 } }];

// [rc4l] The floor in front of the shared wall, where a light ignoring its links paints red.
export const BOXES = { MAP01: { floor: { box: [0.1, 0.45, 0.9, 0.95], ceiling: 3 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "lightlinks", wad: buildLightLinksWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
