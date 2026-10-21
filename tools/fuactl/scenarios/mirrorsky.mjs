// [rc4l] A mirror beside a window onto a low sky-roofed yard behind its wall, GL against Vulkan: exit 0 when Vulkan reflects the room as GL does, with no sky painted over it.
//
// usage:  node scenarios/mirrorsky.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";

// [rc4l] WDI18's bedroom: the mirror pass drew the main walk's sky surfaces, and the yard's sky beyond the mirror wall sat between the reflected eye and the glass, so the mirror showed sky.
export function buildMirrorSkyWad() {
  const m = new UdmfMap();
  const room = m.sector({ heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 });
  const sill = m.sector({ heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 });
  const yard = m.sector({ heightfloor: 0, heightceiling: 64, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1", lightlevel: 192 });
  const wall = (x1, y1, x2, y2, s, tex, line = {}) => m.wall(x1, y1, x2, y2, s, { texturemiddle: tex }, line);
  const open = (x1, y1, x2, y2, front, back) => m.opening(x1, y1, x2, y2, front, back, { texturetop: "BROWN1" }, { texturetop: "BROWN1" });
  // [rc4l] The room: the mirror and a window in its north wall, coloured walls elsewhere for the reflection to show.
  wall(0, -512, 0, 512, room, "COMPBLUE");
  wall(0, 512, 384, 512, room, "STARTAN2");
  wall(384, 512, 640, 512, room, "STARTAN2", { special: SPECIAL.Line_Mirror });
  open(640, 512, 768, 512, room, sill);
  wall(768, 512, 1024, 512, room, "STARTAN2");
  wall(1024, 512, 1024, -512, room, "COMPBLUE");
  wall(1024, -512, 0, -512, room, "BRICK1");
  // [rc4l] The window's depth through the wall.
  wall(640, 512, 640, 528, sill, "BROWN1");
  open(768, 528, 640, 528, yard, sill);
  wall(768, 528, 768, 512, sill, "BROWN1");
  // [rc4l] The yard behind the wall, shallower than the player stands from the mirror and its sky low, so the far wall's sky that GL sees through the window lies between the reflected eye and the glass.
  wall(0, 528, 0, 1024, yard, "BROWN1");
  wall(0, 1024, 1024, 1024, yard, "BROWN1");
  wall(1024, 1024, 1024, 528, yard, "BROWN1");
  wall(1024, 528, 768, 528, yard, "BROWN1");
  wall(640, 528, 0, 528, yard, "BROWN1");
  m.thing({ x: 512, y: -200, angle: 80, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] At the mirror and the window beside it.
export const VANTAGES = [{ name: "facing", at: { x: 512, y: -200, z: 0, angle: 80, pitch: 0 } }];

// [rc4l] The upper half of the mirror, where the reflected ceiling is.
export const BOXES = { MAP01: { mirror: { box: [0.37, 0.26, 0.49, 0.39], ceiling: 3 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "mirrorsky", wad: buildMirrorSkyWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
