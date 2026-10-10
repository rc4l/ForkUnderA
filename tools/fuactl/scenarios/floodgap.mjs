// [rc4l] Steps with no lower or upper texture, against the same room with no steps at all: exit 0 when GL's flood paints each gap with the plane beyond it.
//
// usage:  node scenarios/floodgap.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

const LIGHT = 160, PLATFORM = 32, LINTEL = 160;

// [rc4l] A long hall with a raised platform at the north end and a lowered ceiling at the south, their edges left untextured, or (`flush`) the hall at those heights itself.
function hall(flush) {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1024, 2048, { heightfloor: 0, heightceiling: 256, texturefloor: flush ? "FLAT14" : "FLOOR4_8",
    textureceiling: flush ? "CEIL5_1" : "CEIL3_5", lightlevel: LIGHT }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  if (!flush) {
    m.block(128, 1400, 896, 1900, { heightfloor: PLATFORM, heightceiling: 256, texturefloor: "FLAT14", textureceiling: "CEIL3_5",
      lightlevel: LIGHT }, room);
    m.block(128, 150, 896, 650, { heightfloor: 0, heightceiling: LINTEL, texturefloor: "FLOOR4_8", textureceiling: "CEIL5_1",
      lightlevel: LIGHT }, room);
  }
  m.thing({ x: 512, y: 1024, angle: 90, type: 1 });
  return m;
}

// [rc4l] MAP02's hall is MAP01's with its floor at the platform's height and its ceiling at the lintel's, wearing their flats, and a pit at floor 0 under each vantage so the eye stands as high as in MAP01.
function flushHall() {
  const m = hall(true);
  m.sectors[0].heightfloor = PLATFORM;
  m.sectors[0].heightceiling = LINTEL;
  for (const { at } of VANTAGES)
    m.block(at.x - 20, at.y - 20, at.x + 20, at.y + 20, { heightfloor: 0, heightceiling: LINTEL, texturefloor: "FLAT14",
      textureceiling: "CEIL5_1", lightlevel: LIGHT }, 0, () => ({ texturebottom: "STARTAN2" }));
  return m;
}

export function buildFloodGapWad() {
  return wad([...mapLumps("MAP01", hall(false)), ...mapLumps("MAP02", flushHall())]);
}

// [rc4l] Facing the platform's untextured step from below its top, and the lintel's untextured face from under it.
export const VANTAGES = [
  { name: "step", at: { x: 512, y: 1200, z: 0, angle: 90, pitch: 0 } },
  { name: "lintel", at: { x: 512, y: 850, z: 0, angle: 270, pitch: -35 } },
];

// [rc4l] Each gap where it falls on screen: the step's face below the platform's edge, the lintel's above the lowered ceiling.
export const BOXES = { MAP01: {
  step: { box: [0.1, 0.54, 0.9, 0.63], ceiling: 0.05, vantage: "step" },
  lintel: { box: [0.1, 0.44, 0.9, 0.68], ceiling: 0.05, vantage: "lintel" },
} };

// [rc4l] The hall with no steps is what a flooded gap must look like, so the second side loads MAP02.
export const SIDES = [{ name: "gaps" }, { name: "flush", commands: ["map MAP02"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "floodgap", wad: buildFloodGapWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
