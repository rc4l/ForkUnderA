// [rc4l] A launch quit from its console as a player quits, with the backend's frames still in flight and Diligent's messages on: exit 0 when it leaves cleanly, rather than crashing in the frame fence's static destructor, which printed to a console already gone (queue #46).
//
// usage:  node scenarios/quitclean.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareQuit } from "../src/template.mjs";

export function buildQuitCleanWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 256, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 64, y: 128, angle: 0, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Verbose, so the fence's message on its way out reaches Printf every time; the backend alone, and GL with the backend in a window of its own behind it, as the report had it.
export const SIDES = [
  { name: "vk", noGl: true, launchCvars: { fua_dg_verbose: 1 } },
  { name: "beside", launchCvars: { fua_dg_verbose: 1, fua_dg_embed: 0 } },
];

runScenarioIfMain(import.meta.url, () => ({ name: "quitclean", wad: buildQuitCleanWad(), maps: ["MAP01"], sides: SIDES }), compareQuit);
