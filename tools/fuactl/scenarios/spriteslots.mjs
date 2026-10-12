// [rc4l] Animated decorations in view and behind the player on Vulkan: exit 0 when no sprite frame is drawn from a material slot the bindless array was not yet filled with.
//
// usage:  node scenarios/spriteslots.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareCounter } from "../src/template.mjs";

// [rc4l] AoW's monsters (queue #33): a sprite frame drawn for the first time took its slot after the array was filled, so it was a white quad for one frame; every animated decoration here does the same on each new frame.
export function buildSpriteSlotsWad() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  // [rc4l] North of the player, then south, so turning round brings frames into view that have never been drawn.
  for (const [x, y, type] of [[320, 768, THING.TechLamp], [448, 768, THING.BurningBarrel], [576, 768, THING.EvilEye],
    [704, 768, THING.HeartColumn], [384, 160, THING.HeartColumn], [640, 160, THING.TechLamp]])
    m.thing({ x, y, angle: 90, type });
  m.thing({ x: 512, y: 512, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [
  { name: "north", at: { x: 512, y: 448, z: 0, angle: 90, pitch: 0 } },
  { name: "south", at: { x: 512, y: 448, z: 0, angle: 270, pitch: 0 } },
];

// [rc4l] fua_dg_dynstats counts every dynamic slot past the fill since the level loaded; each one is a sprite drawn white for a frame.
export const COUNTER = { command: "fua_dg_dynstats", label: "sprite slots past the fill", pattern: "past the fill \\((\\d+) since load\\)", ceiling: 0 };

runScenarioIfMain(import.meta.url, () => ({ name: "spriteslots", wad: buildSpriteSlotsWad(), maps: ["MAP01"],
  vantages: VANTAGES, counter: COUNTER }), compareCounter);
