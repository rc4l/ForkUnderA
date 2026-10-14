// [rc4l] A light below a platform's edge with r_lights_checkside off, GL against Vulkan: exit 0 when Vulkan lights the platform's top from below as GL does once the side test is off.
//
// usage:  node scenarios/checkside.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, GL_VS_VULKAN } from "../src/template.mjs";

// [rc4l] A dark room whose north half is a platform 64 high, and a light low in the south half beside its edge: the platform's top is above the light, a floor the light is below.
export function buildCheckSideWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 48 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.block(64, 512, 960, 960, { heightfloor: 64, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 48 },
    room, () => ({ texturebottom: "STARTAN2" }));
  m.thing({ x: 512, y: 470, height: 16, type: 9800, arg0: 255, arg1: 200, arg2: 120, arg3: 220 });
  m.thing({ x: 512, y: 128, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] On the platform, looking down at its top running to the edge the light is under.
export const VANTAGES = [{ name: "platform", at: { x: 512, y: 760, z: 64, angle: 270, pitch: 25 } }];

// [rc4l] The platform's top near the edge, which the light below reaches only with the side test off.
export const BOXES = { MAP01: { top: { box: [0.2, 0.55, 0.8, 0.95], ceiling: 3 } } };

export const SIDES = GL_VS_VULKAN.map((s) => ({ ...s, cvars: { r_lights_checkside: 0 } }));

runScenarioIfMain(import.meta.url, () => ({ name: "checkside", wad: buildCheckSideWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
