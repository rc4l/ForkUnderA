// [rc4l] A polyobject wall facing the player, GL against Vulkan: exit 0 when Vulkan maps the texture across it as GL does, not one stretched column.
//
// usage:  node scenarios/polyobjwall.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL, ENGINE_THING } from "../src/doomnums.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 255 };

export function buildPolyobjWallWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  // [rc4l] The polyobject's template, a 128 by 8 box walked counter-clockwise so every face points out, in a room of its own.
  const tpl = m.room(1024, 0, 1280, 256, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  const side = { texturemiddle: "BIGDOOR1" };
  m.wall(1088, 124, 1216, 124, tpl, side, { special: SPECIAL.Polyobj_StartLine, arg0: 1 });
  m.wall(1216, 124, 1216, 132, tpl, side);
  m.wall(1216, 132, 1088, 132, tpl, side);
  m.wall(1088, 132, 1088, 124, tpl, side);
  m.thing({ x: 1152, y: 128, angle: 1, type: ENGINE_THING.PolyAnchor });
  // [rc4l] Moved to the north of the player's room, its south face toward them.
  m.thing({ x: 256, y: 384, angle: 1, type: ENGINE_THING.PolySpawn });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "door", at: { x: 256, y: 128, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The door's face, where a u stuck at 0 smears one column of BIGDOOR1 into horizontal bands.
export const BOXES = { MAP01: { door: { box: [0.35, 0.25, 0.65, 0.75], ceiling: 6 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "polyobjwall", wad: buildPolyobjWallWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
