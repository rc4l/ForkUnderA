// [rc4l] A polyobject a script slides after the level is baked, GL against Vulkan: exit 0 when Vulkan draws it where it moved to, not where it was baked.
//
// usage:  node scenarios/polyobjmove.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL, ENGINE_THING } from "../src/doomnums.mjs";
import { acs0, callSpecial, PCD } from "../src/acs.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 255 };

export function buildPolyobjMoveWad() {
  const m = new UdmfMap();
  m.room(0, 0, 768, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 384, y: 96, angle: 90, type: 1 });
  // [rc4l] The polyobject's template, a 128 by 8 box walked counter-clockwise so every face points out, in a room of its own.
  const tpl = m.room(1024, 0, 1280, 256, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  const side = { texturemiddle: "BIGDOOR1" };
  m.wall(1088, 124, 1216, 124, tpl, side, { special: SPECIAL.Polyobj_StartLine, arg0: 1 });
  m.wall(1216, 124, 1216, 132, tpl, side);
  m.wall(1216, 132, 1088, 132, tpl, side);
  m.wall(1088, 132, 1088, 124, tpl, side);
  m.thing({ x: 1152, y: 128, angle: 1, type: ENGINE_THING.PolyAnchor });
  // [rc4l] Spawned left of the player's line of sight, its south face toward them.
  m.thing({ x: 256, y: 384, angle: 1, type: ENGINE_THING.PolySpawn });
  // [rc4l] Closed script 2: six tics after it is run, the polyobject slides 256 east at 8 a tic.
  const behavior = acs0([PCD.DELAYDIRECT, 6, ...callSpecial(SPECIAL.Polyobj_Move, [1, 64, 0, 256]), PCD.TERMINATE],
    [], { open: false, number: 2 });
  return wad(mapLumps("MAP01", m, { behavior }));
}

export const VANTAGES = [{ name: "door", at: { x: 384, y: 96, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] Where the door was and where it went, so drawing it at either place alone fails.
export const BOXES = { MAP01: { door: { box: [0.15, 0.25, 0.85, 0.75], ceiling: 6 } } };

export const SIDES = [{ name: "gl", gl: true, commands: ["puke 2"] }, { name: "vk", gl: false, commands: ["puke 2"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "polyobjmove", wad: buildPolyobjMoveWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
