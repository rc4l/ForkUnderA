// [rc4l] A one-sided wall wearing a see-through texture with a bright room behind it, GL against Vulkan: exit 0 when Vulkan draws the texture's holes in their own colour, as GL's solid pass does, instead of opening them onto the room.
//
// usage:  node scenarios/plainwall.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

export function buildPlainWallWad() {
  const m = new UdmfMap();
  m.room(0, 0, 256, 256, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, (edge) => ({ side: { texturemiddle: edge === "east" ? "MIDGRATE" : "STARTAN2" } }));
  // [rc4l] A separate, fully lit room just past the grate, which only a discarded texel can show.
  m.room(264, 0, 520, 256, { heightfloor: 0, heightceiling: 128, texturefloor: "FLAT14", textureceiling: "CEIL3_5",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "BROWN1" } }));
  m.thing({ x: 96, y: 128, angle: 0, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Across the room, and pressed against the grate, where each texel is many pixels and GL's smoothed edge texels (SmoothEdges) show as a rim on every bar (queue #39's fan).
export const VANTAGES = [
  { name: "grate", at: { x: 96, y: 128, z: 0, angle: 0, pitch: 0 } },
  { name: "close", at: { x: 238, y: 128, z: 0, angle: 0, pitch: 0 } },
];

export const BOXES = { MAP01: {
  grate: { vantage: "grate", box: [0.2, 0.2, 0.8, 0.8], ceiling: 6 },
  close: { vantage: "close", box: [0.1, 0.1, 0.9, 0.8], block: 1, ceiling: 0.5 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "plainwall", wad: buildPlainWallWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
