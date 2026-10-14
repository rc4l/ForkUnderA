// [rc4l] A line mirror facing a row of pillars behind the player, GL against Vulkan in standalone mode: exit 0 when Vulkan's mirror shows the pillars as GL's does, from a walk of the mirror's own each frame.
//
// usage:  node scenarios/mirrorsprite.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";

// [rc4l] linemirror's room, the mirror the north wall; the pillars stand south of the player, seen only in it.
export function buildMirrorSpriteWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, (edge) => edge === "north"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Line_Mirror } }
    : { side: { texturemiddle: edge === "south" ? "BRICK1" : "COMPBLUE" } });
  // [rc4l] Clear of the middle, where the player's own reflection stands.
  for (const x of [112, 176, 336, 400]) m.thing({ x, y: 150, angle: 90, type: THING.TechPillar });
  m.thing({ x: 256, y: 380, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "facing", at: { x: 256, y: 380, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The middle of the mirror, where the pillars and the player stand reflected.
export const BOXES = { MAP01: { mirror: { box: [0.37, 0.35, 0.63, 0.63], ceiling: 2 } } };

export const SIDES = [{ name: "gl", gl: true }, { name: "standalone", cvars: { fua_dg_standalone: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "mirrorsprite", wad: buildMirrorSpriteWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
