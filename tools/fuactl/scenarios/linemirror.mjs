// [rc4l] A room with a one-sided Line_Mirror wall, GL against Vulkan: exit 0 when Vulkan reflects the room and the player as GL does.
//
// usage:  node scenarios/linemirror.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";

// [rc4l] The mirror is the north wall; a lamp and coloured walls behind the player give the reflection something to show.
export function buildLineMirrorWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, (edge) => edge === "north"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Line_Mirror } }
    : { side: { texturemiddle: edge === "south" ? "BRICK1" : "COMPBLUE" } });
  m.thing({ x: 160, y: 96, angle: 90, type: THING.Column });
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Facing the mirror from the middle of the room, and from off to one side.
export const VANTAGES = [
  { name: "facing", at: { x: 256, y: 256, z: 0, angle: 90, pitch: 0 } },
  { name: "oblique", at: { x: 384, y: 200, z: 0, angle: 110, pitch: 0 } },
];

// [rc4l] The mirror's face, above the weapon.
export const BOXES = { MAP01: { mirror: { box: [0.1, 0.12, 0.9, 0.62], ceiling: 2 } } };

// [rc4l] GL's faint sphere-mapped sheen over a mirror (gl_mirror_envmap) is not drawn by the backend yet, so the reflection is compared without it.
export const SIDES = [{ name: "gl", gl: true, cvars: { gl_mirror_envmap: "0" } }, { name: "vk", gl: false }];

runScenarioIfMain(import.meta.url, () => ({ name: "linemirror", wad: buildLineMirrorWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
