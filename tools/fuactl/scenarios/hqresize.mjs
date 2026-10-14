// [rc4l] hqresize switched on in play, GL against Vulkan: exit 0 when Vulkan throws its textures away and remakes them scaled, as GL's flush does.
//
// usage:  node scenarios/hqresize.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, GL_VS_VULKAN } from "../src/template.mjs";

// [rc4l] A wall close up, its texels large enough on screen that scaling them shows.
export function buildHqResizeWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 400, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "wall", at: { x: 256, y: 440, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The wall in front, above the weapon.
export const BOXES = { MAP01: { wall: { box: [0.1, 0.1, 0.9, 0.6], ceiling: 3 } } };

// [rc4l] Set once the level is up, so the textures it already made must be flushed and remade.
export const SIDES = GL_VS_VULKAN.map((s) => ({ ...s, cvars: { r_texture_hqresize: 4 } }));

runScenarioIfMain(import.meta.url, () => ({ name: "hqresize", wad: buildHqResizeWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
