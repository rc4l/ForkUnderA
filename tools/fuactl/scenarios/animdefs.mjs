// [rc4l] An ANIMDEFS wall that changes frame after the level is built, GL against Vulkan: exit 0 when Vulkan shows the frame GL shows.
//
// usage:  node scenarios/animdefs.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] STARTAN2 for the first second, then BROWN1 for the rest of the run, so a backend still drawing the frame it baked shows a different texture from GL.
const ANIMDEFS = `texture STARTAN2
\tpic STARTAN2 tics 35
\tpic BROWN1 tics 30000
`;

export function buildAnimdefsWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  return wad([{ name: "ANIMDEFS", data: ANIMDEFS }, ...mapLumps("MAP01", m)]);
}

// [rc4l] Facing the north wall from the south end of the room.
export const VANTAGES = [{ name: "wall", at: { x: 256, y: 128, z: 0, angle: 90, pitch: 0 } }];

export const BOXES = { MAP01: { wall: { box: [0.1, 0.2, 0.9, 0.7], ceiling: 6 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "animdefs", wad: buildAnimdefsWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
