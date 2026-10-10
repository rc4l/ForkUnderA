// [rc4l] A room whose ceiling is a stacked sector opening onto a brighter room off the map's side: exit 0 when Vulkan shows that room through the ceiling as GL does.
//
// usage:  node scenarios/stackportal.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] The upper room sits east by the stack's displacement, as WDI01's rooftops sit east of its town.
export function buildStackPortalWad() {
  const m = new UdmfMap();
  m.stackedRooms();
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
