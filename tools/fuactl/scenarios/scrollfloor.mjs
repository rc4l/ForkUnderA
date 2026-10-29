// [rc4l] A floor scrolled by its sector type beside a still one, Vulkan captured twice apart: exit 0 when the scrolled floor moved and the still one did not.
//
// usage:  node scenarios/scrollfloor.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareOverTime } from "../src/template.mjs";
import { SECTOR_TYPE } from "../src/doomnums.mjs";

// [rc4l] The scroller moves only the plane's texture offsets, as WDI09's slime floor's Carry_East5 does, and the map bake froze it at its first offset.
export function buildScrollFloorWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.block(256, 512, 768, 896, { heightfloor: 0, heightceiling: 256, texturefloor: "FLAT14", textureceiling: "CEIL3_5",
    lightlevel: 192, special: SECTOR_TYPE.Scroll_East_Fast }, room);
  m.thing({ x: 512, y: 128, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] From the still floor, looking down at the scrolled one beyond it, off it so the scroller cannot carry the camera.
export const VANTAGES = [{ name: "scroll", at: { x: 512, y: 420, z: 0, angle: 90, pitch: 20 } }];

// [rc4l] Per pixel, as warpwater judges a moving floor; the still floor in front must hold.
export const BOXES = { MAP01: {
  scrolled: { box: [0.35, 0.2, 0.65, 0.42], floor: 1, block: 1 },
  still: { box: [0.3, 0.85, 0.7, 0.95], ceiling: 0.05, block: 1 },
} };

// [Doruk-Aksoy] Stepped, not timed: FLAT14 repeats every 64 units and the scroller moves 2 a tic, so captures 32 tics apart match, and 600 ms plus a capture's own time landed there.
export const STEP_TICS = 5;

runScenarioIfMain(import.meta.url, () => ({ name: "scrollfloor", wad: buildScrollFloorWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, stepTics: STEP_TICS }), compareOverTime);
