// [rc4l] gl_seamless on Vulkan: exit 0 when the bake splits a wall's fan where a third sector's planes meet its end, as GL's SplitRightEdge does, so no T-junction there can crack.
//
// usage:  node scenarios/seamless.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareCounter } from "../src/template.mjs";

const STEP = { texturetop: "STARTAN2", texturebottom: "STARTAN2" };

// [rc4l] Two blocks side by side in a room, B (64..192) and C (32..224), so the corner the room, B and C share holds six heights; returns the wad and B's south line, whose room side has an upper and a lower part ending there.
export function buildSeamlessMap() {
  const m = new UdmfMap();
  const a = m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "BRICK1" } }));
  const b = m.sector({ heightfloor: 64, heightceiling: 192, texturefloor: "FLAT14", textureceiling: "FLAT14", lightlevel: 192 });
  const c = m.sector({ heightfloor: 32, heightceiling: 224, texturefloor: "FLAT14", textureceiling: "FLAT14", lightlevel: 192 });
  const target = m.opening(256, 128, 128, 128, b, a, {}, STEP);
  m.opening(128, 128, 128, 384, b, a, {}, STEP);
  m.opening(128, 384, 256, 384, b, a, {}, STEP);
  m.opening(256, 384, 256, 128, b, c, STEP, STEP);
  m.opening(256, 384, 384, 384, c, a, {}, STEP);
  m.opening(384, 384, 384, 128, c, a, {}, STEP);
  m.opening(384, 128, 256, 128, c, a, {}, STEP);
  m.thing({ x: 192, y: 32, angle: 90, type: 1 });
  return { wad: wad(mapLumps("MAP01", m)), target };
}

// [rc4l] In front of B's south face, where the walk reaches it.
export const VANTAGES = [{ name: "facing", at: { x: 192, y: 32, z: 0, angle: 90, pitch: 0 } }];

export const SIDES = [{ name: "vk", cvars: { gl_seamless: 1 } }];

// [rc4l] fua_line_mesh lists the line's pieces: its upper (piece 0) holds 6 vertices as a plain quad, and 9 once its right edge splits at C's ceiling.
export function seamlessCounter(target) {
  return { command: `fua_line_mesh ${target}`, label: "vertices in the split upper", pattern: "piece 0: range \\d+\\+(\\d+)", floor: 9 };
}

runScenarioIfMain(import.meta.url, () => {
  const { wad: w, target } = buildSeamlessMap();
  return { name: "seamless", wad: w, maps: ["MAP01"], vantages: VANTAGES, sides: SIDES, counter: seamlessCounter(target) };
}, compareCounter);
