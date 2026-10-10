// [rc4l] A skybox sky above a reflective floor, Vulkan with the reflection against without: exit 0 when every box is under its ceiling.
//
// usage:  node scenarios/skymirror.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { acs0, callSpecial, PCD } from "../src/acs.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";

const POOL_TAG = 125;

// [rc4l] A sky court standing in a taller roofed hall, a reflective pool in it, and the skybox room the sky is seen from.
export function buildSkymirrorWad() {
  const m = new UdmfMap();
  const hall = m.room(0, 0, 2048, 2048, { heightfloor: 0, heightceiling: 1024, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  // [rc4l] Only the court's sky walls hide the hall's walls above 256, so a sky wall lost to the mirror's walk shows them through the sky.
  const court = m.block(256, 128, 1792, 1792, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8",
    textureceiling: "F_SKY1", lightlevel: 192 }, hall);
  // [rc4l] GL draws the pool through a plane-mirror portal, whose walk re-enters the scene from under the floor.
  m.block(900, 420, 1150, 640, { heightfloor: -16, heightceiling: 256, texturefloor: "FLAT14", textureceiling: "F_SKY1",
    lightlevel: 192, id: POOL_TAG }, court, () => ({ texturebottom: "BRICK1" }));
  m.thing({ x: 1024, y: 300, angle: 90, type: 1 });
  m.skyboxRoom(4000, 4000);
  const script = acs0([...callSpecial(SPECIAL.Sector_SetPlaneReflection, [POOL_TAG, 66, 0]), PCD.TERMINATE]);
  return wad(mapLumps("MAP01", m, { behavior: script }));
}

// [rc4l] The pool low in the frame keeps its mirror walk running while walls its reflection cannot see carry the sky above.
export const VANTAGES = [
  { name: "north-west", at: { x: 1500, y: 250, z: 0, angle: 135, pitch: -10 } },
  { name: "west",       at: { x: 1024, y: 200, z: 0, angle: 150, pitch: -8 } },
  { name: "up",         at: { x: 1300, y: 350, z: 0, angle: 100, pitch: -25 } },
  { name: "level",      at: { x: 1024, y: 300, z: 0, angle: 90, pitch: 0 } },
];

// [rc4l] Above the pool's row and below the notify lines, where only walls and sky are.
export const BOXES = { MAP01: { sky: { box: [0.0, 0.05, 1.0, 0.5], ceiling: 0.05 } } };

// [rc4l] The pool's reflection must not change the sky: the mirror's walk once replaced the player's sky walls with its own.
export const SIDES = [{ name: "plain", cvars: { gl_plane_reflection: 0 } }, { name: "reflecting", cvars: { gl_plane_reflection: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "skymirror", wad: buildSkymirrorWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
