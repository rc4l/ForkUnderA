// [rc4l] A 3D floor platform a script raises after the level starts, GL against Vulkan: exit 0 when Vulkan shows it where it rose to, as GL does.
//
// usage:  node scenarios/raised3dfloor.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { acs0, callSpecial, PCD } from "../src/acs.mjs";

const TARGET_TAG = 3, CONTROL_TAG = 4;

// [rc4l] WDI09's shaft platform: only its control sector moves, and the bake re-baked that sector's own walls and none of the platform's.
export function buildRaised3DFloorWad() {
  const m = new UdmfMap();
  const hall = m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 384, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 192 }, () => ({ side: { texturemiddle: "BROWN1" } }));
  m.block(256, 384, 768, 768, { heightfloor: 0, heightceiling: 384, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192, id: TARGET_TAG }, hall);
  // [rc4l] The platform is the control room's floor to ceiling, 64 to 96, and STARTAN2 is its side.
  m.room(2000, 2000, 2064, 2064, { heightfloor: 64, heightceiling: 96, texturefloor: "FLAT14", textureceiling: "FLAT14",
    lightlevel: 192, id: CONTROL_TAG }, (edge) => (edge === "west"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Sector_Set3DFloor, arg0: TARGET_TAG, arg1: 1, arg2: 0, arg3: 255 } }
    : { side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 512, y: 128, angle: 90, type: 1 });
  // [rc4l] Half a second in, after the level is baked, the platform rises 128 at 8 a tic.
  const behavior = acs0([PCD.DELAYDIRECT, 18, ...callSpecial(SPECIAL.FloorAndCeiling_RaiseByValue, [CONTROL_TAG, 64, 128]), PCD.TERMINATE]);
  return wad(mapLumps("MAP01", m, { behavior }));
}

// [rc4l] From the hall's south side at the platform's face, a little upward so both heights are in frame.
export const VANTAGES = [{ name: "platform", at: { x: 512, y: 128, z: 0, angle: 90, pitch: -10 } }];

// [rc4l] Where the platform was and where it rose to.
export const BOXES = { MAP01: { platform: { box: [0.25, 0.1, 0.75, 0.6], ceiling: 2 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "raised3dfloor", wad: buildRaised3DFloorWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
