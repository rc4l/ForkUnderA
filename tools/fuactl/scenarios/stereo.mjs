// [rc4l] GL's stereo 3D, GL against Vulkan: exit 0 when Vulkan draws vr_mode's red/cyan anaglyph as GL does, each eye from its own place through its colour mask, and its left eye alone (vr_mode 5) likewise.
//
// usage:  node scenarios/stereo.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, GL_VS_VULKAN } from "../src/template.mjs";
import { THING } from "../src/doomnums.mjs";

// [rc4l] A room with a column near the eye, where the two eyes see it furthest apart.
export function buildStereoWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    (edge) => ({ side: { texturemiddle: edge === "north" ? "STARTAN2" : edge === "east" ? "COMPBLUE" : "BRICK1" } }));
  m.thing({ x: 256, y: 200, angle: 90, type: THING.Column });
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "facing", at: { x: 256, y: 64, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The column and the wall behind it, where the eyes' pictures part.
export const BOXES = { MAP01: { scene: { box: [0.3, 0.2, 0.7, 0.75], ceiling: 3 } } };

// [rc4l] Run once with the anaglyph and once with the left eye alone.
export function stereoSides(mode) {
  return GL_VS_VULKAN.map((s) => ({ ...s, commands: [`vr_mode ${mode}`] }));
}

runScenarioIfMain(import.meta.url, () => ({ name: "stereo", wad: buildStereoWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: stereoSides(Number(process.env.FUA_VRMODE || 2)) }));
