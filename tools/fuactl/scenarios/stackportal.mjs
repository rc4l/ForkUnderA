// [rc4l] A room whose ceiling is a stacked sector opening onto a brighter room off the map's side: exit 0 when Vulkan shows that room through the ceiling as GL does.
//
// usage:  node scenarios/stackportal.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { THING } from "../src/doomnums.mjs";

// [rc4l] The pair of anchors share a TID; the upper room sits STACK_DX east, the stack's displacement, as WDI01's rooftops sit east of its town.
const STACK_TID = 7, STACK_DX = 4096;

export function buildStackPortalWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 128 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.room(STACK_DX, 0, STACK_DX + 512, 512, { heightfloor: 128, heightceiling: 640, texturefloor: "FLOOR4_8",
    textureceiling: "F_SKY1", lightlevel: 255 }, () => ({ side: { texturemiddle: "BRICK1" } }));
  // [rc4l] Off the eye's line so a sky rendered from an anchor, the old fallback, sees the room from somewhere else.
  m.thing({ x: 64, y: 64, angle: 0, type: THING.LowerStackLookOnly, id: STACK_TID });
  m.thing({ x: STACK_DX + 64, y: 64, angle: 0, type: THING.UpperStackLookOnly, id: STACK_TID });
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Looking up through the stacked ceiling at the upper room's far walls and its sky, as from WDI01's courtyard.
export const VANTAGES = [
  { name: "up-south-west", at: { x: 448, y: 448, z: 0, angle: 225, pitch: -40 } },
  { name: "up-north",      at: { x: 256, y: 96, z: 0, angle: 90, pitch: -30 } },
];

// [rc4l] The top of the frame, where only the stacked ceiling and what lies through it are.
export const BOXES = { MAP01: { through: { box: [0.0, 0.05, 1.0, 0.4], ceiling: 1 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "stackportal", wad: buildStackPortalWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
