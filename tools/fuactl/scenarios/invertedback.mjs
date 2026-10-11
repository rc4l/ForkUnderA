// [rc4l] A wall onto a sector whose floor is above its ceiling, GL alone against GL in a Vulkan session: exit 0 when GL draws that wall whole in both, so the backend's wall mesh never changes GL's picture.
//
// usage:  node scenarios/invertedback.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] WDI18's mirror frame: GL treats the inverted back sector as closed and draws one bottom wall there, the map bake derives a top too, and GL replayed its bottom from the map bake's top slot, leaving a hole onto the room behind.
export function buildInvertedBackWad() {
  const m = new UdmfMap();
  // [rc4l] The room's ceiling meets the inverted sector's floor, so GL draws no top on the wall between them.
  const room = m.sector({ heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 160 });
  const inverted = m.sector({ heightfloor: 128, heightceiling: 88, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 160 });
  const beyond = m.sector({ heightfloor: 0, heightceiling: 128, texturefloor: "FLAT5_3", textureceiling: "CEIL3_5", lightlevel: 255 });
  const tiers = { texturetop: "BROWN1", texturebottom: "BROWN1" };
  const wall = (x1, y1, x2, y2, s, tex) => m.wall(x1, y1, x2, y2, s, { texturemiddle: tex });
  // [rc4l] The room, its north wall half onto the inverted strip and half open onto the room beyond.
  wall(0, 0, 0, 512, room, "STARTAN2");
  m.opening(0, 512, 512, 512, room, inverted, tiers, tiers);
  m.opening(512, 512, 1024, 512, room, beyond);
  wall(1024, 512, 1024, 0, room, "STARTAN2");
  wall(1024, 0, 0, 0, room, "STARTAN2");
  // [rc4l] The inverted strip, sixteen deep, with the room beyond on its other two sides.
  wall(0, 512, 0, 528, inverted, "BROWN1");
  m.opening(0, 528, 512, 528, inverted, beyond, {}, tiers);
  m.opening(512, 528, 512, 512, inverted, beyond, {}, tiers);
  // [rc4l] The room beyond, red at the far end, which GL draws whole since the opening shows part of it.
  wall(0, 528, 0, 1024, beyond, "BROWN1");
  wall(0, 1024, 1024, 1024, beyond, "REDWALL");
  wall(1024, 1024, 1024, 512, beyond, "BROWN1");
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Square on to the wall over the inverted strip.
export const VANTAGES = [{ name: "wall", at: { x: 256, y: 256, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The wall's lower part, below the map bake's top tier at 88.
export const BOXES = { MAP01: { wall: { box: [0.1, 0.45, 0.45, 0.7], ceiling: 2 } } };

// [rc4l] GL alone is the reference; the second is GL's frame from a Vulkan session, where replayed walls draw from the backend's mesh.
export const SIDES = [{ name: "gl", gl: true }, { name: "glmesh", frame: "gl" }];

runScenarioIfMain(import.meta.url, () => ({ name: "invertedback", wad: buildInvertedBackWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
