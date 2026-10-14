// [rc4l] A reflective floor in front of a raised block, GL against Vulkan: exit 0 when Vulkan paints the block's reflection where GL does.
//
// usage:  node scenarios/planemirror.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { acs0, callSpecial, PCD } from "../src/acs.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";

const FLOOR_TAG = 7;

export function buildPlaneMirrorWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLAT1", textureceiling: "CEIL3_5",
    lightlevel: 200, id: FLOOR_TAG }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  // [rc4l] A bright block standing on the mirror, whose reflection hangs below it.
  m.block(384, 640, 640, 768, { heightfloor: 96, heightceiling: 256, texturefloor: "FLAT14", textureceiling: "CEIL3_5",
    lightlevel: 200 }, room, () => ({ texturebottom: "COMPBLUE" }));
  m.thing({ x: 512, y: 200, angle: 90, type: 1 });
  const script = acs0([...callSpecial(SPECIAL.Sector_SetPlaneReflection, [FLOOR_TAG, 160, 0]), PCD.TERMINATE]);
  return wad(mapLumps("MAP01", m, { behavior: script }));
}

// [rc4l] From the south, looking down at the floor in front of the block.
export const VANTAGES = [{ name: "front", at: { x: 512, y: 300, z: 0, angle: 90, pitch: 20 } }];

// [rc4l] The floor between the eye and the block, where the block's reflection is.
export const BOXES = { MAP01: { mirror: { box: [0.3, 0.55, 0.7, 0.85], ceiling: 4 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "planemirror", wad: buildPlaneMirrorWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
