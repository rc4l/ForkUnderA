// [rc4l] A stacked ceiling onto an upper room twice as wide as the room under it, GL against Vulkan: exit 0 when the engine reaches the level and both show the upper room, where it used to die of a stack overflow.
//
// usage:  node scenarios/stackwide.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] The upper room runs 512 past the stacked room's east wall, which is what made each stacked view draw the next before itself.
export function buildStackWideWad() {
  const m = new UdmfMap();
  m.stackedRooms({ upperWidth: 1024 });
  m.thing({ x: 64, y: 256, angle: 0, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "up-east", at: { x: 64, y: 256, z: 0, angle: 0, pitch: -30 } }];

export const BOXES = { MAP01: { through: { box: [0.0, 0.05, 1.0, 0.4], ceiling: 1 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "stackwide", wad: buildStackWideWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
