// [rc4l] A lake whose surface is a height transfer's fake floor, GL against Vulkan: exit 0 when Vulkan shows the surface at the control sector's height from the shore, and under it the control sector's floor, underside and blue light, as GL does.
//
// usage:  node scenarios/transferheights.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] AOW03's lake (queue #35, #36): the bake drew a Transfer_Heights sector's own floor 96 below the surface GL shows, and crouched under the surface the above-water scene instead of GL's underwater one.
export function buildTransferHeightsWad() {
  const m = new UdmfMap();
  const shore = m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  const lake = m.block(256, 384, 768, 896, { heightfloor: -128, heightceiling: 256, texturefloor: "FWATER1", textureceiling: "F_SKY1", lightlevel: 192, id: 5 },
    shore, () => ({}), () => ({ texturebottom: "BROWN1" }));
  // [rc4l] A step in the lake under the same transfer, high enough that the eye of a player standing on it is 2 above the surface, and still where the water animates by the clock.
  m.block(640, 420, 704, 484, { heightfloor: -71, heightceiling: 256, texturefloor: "FLAT14", textureceiling: "F_SKY1", lightlevel: 192, id: 5 },
    lake, () => ({ texturebottom: "BROWN1" }));
  // [rc4l] The control sector, sealed away: its floor is the surface, its floor and ceiling textures what the eye sees under it, its light colour the water's.
  m.room(3000, 3000, 3128, 3128, { heightfloor: -32, heightceiling: 512, texturefloor: "FLAT5_3", textureceiling: "FWATER1", lightlevel: 192,
    lightcolor: 0x0070ff }, (edge) => ({ side: { texturemiddle: "STARTAN2" },
    line: edge === "west" ? { special: SPECIAL.Transfer_Heights, arg0: 5 } : {} }));
  m.thing({ x: 512, y: 128, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] From the shore looking down at the lake, from the lake bed, the eye 41 above it and so under the surface, and from the step, the eye 2 over the surface and nearer it than the near plane.
export const VANTAGES = [
  { name: "shore", at: { x: 512, y: 200, z: 0, angle: 90, pitch: 30 } },
  { name: "under", at: { x: 512, y: 500, z: -128, angle: 90, pitch: 0 } },
  { name: "waterline", at: { x: 672, y: 452, z: -71, angle: 90, pitch: 35 } },
];

// [rc4l] The lake from the shore, under the surface the bed below and the underside above, and at the waterline the surface right under the eye, which GL's depth clamp keeps.
export const BOXES = { MAP01: {
  lake: { vantage: "shore", box: [0.25, 0.12, 0.75, 0.25], ceiling: 6 },
  under: { vantage: "under", box: [0.05, 0.1, 0.95, 0.85], ceiling: 6 },
  waterline: { vantage: "waterline", box: [0.1, 0.62, 0.9, 0.85], ceiling: 6 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "transferheights", wad: buildTransferHeightsWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
