// [rc4l] A platform a script raises after Vulkan has taken over the frame, GL alone against Vulkan in standalone mode: exit 0 when Vulkan shows it risen, re-baked from the map with no GL walk behind it.
//
// usage:  node scenarios/standalonemove.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { acs0, callSpecial, PCD } from "../src/acs.mjs";

const PLATFORM_TAG = 5;

// [rc4l] Queue #42's comparison runs Vulkan standalone, whose startup line still claimed moving geometry was not fed.
export function buildStandaloneMoveWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  // [rc4l] The platform's faces are red, so it shows red only once it has risen.
  m.block(160, 300, 352, 420, { heightfloor: 0, heightceiling: 256, texturefloor: "FLAT14", textureceiling: "CEIL3_5",
    lightlevel: 192, id: PLATFORM_TAG }, room, () => ({ texturebottom: "REDWALL" }));
  m.thing({ x: 256, y: 120, angle: 90, type: 1 });
  // [rc4l] Closed script 2: six tics after it is run, once the standalone bake is done, the platform rises 64 at 8 a tic.
  const behavior = acs0([PCD.DELAYDIRECT, 6, ...callSpecial(SPECIAL.Floor_RaiseByValue, [PLATFORM_TAG, 64, 64]), PCD.TERMINATE],
    [], { open: false, number: 2 });
  return wad(mapLumps("MAP01", m, { behavior }));
}

export const VANTAGES = [{ name: "platform", at: { x: 256, y: 120, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The platform's red face and the floor beyond its old edge.
export const BOXES = { MAP01: { platform: { box: [0.25, 0.35, 0.75, 0.75], ceiling: 4 } } };

// [rc4l] The standalone side switches on before the script runs, so the rise happens with GL no longer walking the scene.
export const SIDES = [{ name: "gl", gl: true, commands: ["puke 2"] },
  { name: "standalone", cvars: { fua_dg_standalone: 1 }, commands: ["puke 2"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "standalonemove", wad: buildStandaloneMoveWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
