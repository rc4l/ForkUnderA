// [rc4l] A pane across a room, a two-sided middle texture at half alpha, GL against Vulkan: exit 0 when Vulkan shows the wall behind it through the pane as GL does.
//
// usage:  node scenarios/translucentmid.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] WDI01's windows are this: the map bake took the middle texture and drew it solid, ignoring the linedef's alpha.
export function buildTranslucentMidWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1024, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 192 }, (edge) => ({ side: { texturemiddle: edge === "east" ? "COMPBLUE" : "STARTAN2" } }));
  m.block(480, 64, 544, 448, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, room, (edge) => (edge === "west" ? { texturemiddle: "BRICK1" } : {}), () => ({}),
    (edge) => (edge === "west" ? { alpha: 0.5 } : {}));
  m.thing({ x: 128, y: 256, angle: 0, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Facing the pane square on, and across it at an angle.
export const VANTAGES = [
  { name: "square", at: { x: 128, y: 256, z: 0, angle: 0, pitch: 0 } },
  { name: "angled", at: { x: 160, y: 96, z: 0, angle: 30, pitch: 0 } },
];

// [rc4l] The middle of the frame, where the pane hangs in front of the blue wall.
export const BOXES = { MAP01: { pane: { box: [0.3, 0.2, 0.7, 0.6], ceiling: 2 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "translucentmid", wad: buildTranslucentMidWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
