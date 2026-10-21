// [rc4l] A platform a script raises while it is behind the player, GL against Vulkan in standalone mode: exit 0 when Vulkan shows it risen once the player turns to it, re-baked when a walk first reaches it rather than when it moved.
//
// usage:  node scenarios/movedbehind.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { acs0, callSpecial, PCD } from "../src/acs.mjs";

const PLATFORM_TAG = 5;

export function buildMovedBehindWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  // [rc4l] standalonemove's platform, its faces red so it shows red only once it has risen.
  m.block(160, 300, 352, 420, { heightfloor: 0, heightceiling: 256, texturefloor: "FLAT14", textureceiling: "CEIL3_5",
    lightlevel: 192, id: PLATFORM_TAG }, room, () => ({ texturebottom: "REDWALL" }));
  // [rc4l] Facing south, away from the platform, while it rises.
  m.thing({ x: 256, y: 120, angle: 270, type: 1 });
  // [rc4l] Closed script 2: fifteen tics after it is run, while the first vantage looks away with standalone long since on, the platform rises 64 in one tic.
  const behavior = acs0([PCD.DELAYDIRECT, 15, ...callSpecial(SPECIAL.Floor_RaiseByValue, [PLATFORM_TAG, 512, 64]), PCD.TERMINATE],
    [], { open: false, number: 2 });
  return wad(mapLumps("MAP01", m, { behavior }));
}

// [rc4l] Looking away while it rises, then turned to it.
export const VANTAGES = [
  { name: "away", at: { x: 256, y: 120, z: 0, angle: 270, pitch: 0 } },
  { name: "turned", at: { x: 256, y: 120, z: 0, angle: 90, pitch: 0 } },
];

// [rc4l] The platform's red face and the floor beyond its old edge, once turned to it.
export const BOXES = { MAP01: { platform: { box: [0.25, 0.35, 0.75, 0.75], ceiling: 4, vantage: "turned" } } };

export const SIDES = [{ name: "gl", gl: true, commands: ["puke 2"] },
  { name: "standalone", cvars: { fua_dg_standalone: 1 }, commands: ["puke 2"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "movedbehind", wad: buildMovedBehindWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
