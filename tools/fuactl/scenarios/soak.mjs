// [rc4l] A level loaded again and again in one launch, with the engine counting its memory, objects, handles and threads after each: exit 0 when none keeps growing.
//
// usage:  node scenarios/soak.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareSoak } from "../src/template.mjs";

// [rc4l] Firefox ran out of GPU memory on a map loaded the third time (queue #48), and every new game leaked each player's userinfo cvars.
export function buildSoakWad() {
  const m = new UdmfMap();
  m.room(0, 0, 768, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    (edge) => ({ side: { texturemiddle: edge === "north" ? "REDWALL" : "STARTAN2" } }));
  m.thing({ x: 128, y: 256, angle: 0, type: 1 });
  // [rc4l] A light and a few monsters, so a load makes the dynamic lights and actors a leak would keep.
  m.thing({ x: 384, y: 256, height: 64, type: 9800, arg0: 255, arg1: 200, arg2: 120, arg3: 200 });
  for (let i = 0; i < 4; i++) m.thing({ x: 600, y: 96 + i * 100, angle: 180, type: 3001 });
  return wad(mapLumps("MAP01", m));
}

export const SIDES = [{ name: "vk", noGl: true }];

runScenarioIfMain(import.meta.url, () => ({ name: "soak", wad: buildSoakWad(), maps: ["MAP01"], sides: SIDES, cycles: 6 }), compareSoak);
