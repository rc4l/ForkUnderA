// [rc4l] stackportal's rooms, the player spawning against the east wall and then turning to look up through the stacked ceiling to the west, GL against Vulkan in standalone mode: exit 0 when Vulkan shows the upper room as GL does, walked that frame rather than kept from GL's last walk.
//
// usage:  node scenarios/stackturn.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

export function buildStackTurnWad() {
  const m = new UdmfMap();
  m.stackedRooms();
  // [rc4l] Facing the east wall from close by, so the frames GL walks before standalone takes over see the stacked view only through it.
  m.thing({ x: 480, y: 256, angle: 0, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "up-west", at: { x: 448, y: 256, z: 0, angle: 180, pitch: -35 } }];

// [rc4l] The top of the frame, where only the stacked ceiling and what lies through it are.
export const BOXES = { MAP01: { through: { box: [0.0, 0.05, 1.0, 0.4], ceiling: 1 } } };

export const SIDES = [{ name: "gl", gl: true }, { name: "standalone", cvars: { fua_dg_standalone: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "stackturn", wad: buildStackTurnWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
