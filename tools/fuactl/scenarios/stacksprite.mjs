// [rc4l] A stacked ceiling looking up at things standing in the room above, GL against Vulkan: exit 0 when Vulkan's stacked view draws the sprites GL's walk through it draws.
//
// usage:  node scenarios/stacksprite.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { THING } from "../src/doomnums.mjs";

const STACK_DX = 4096;

export function buildStackSpriteWad() {
  const m = new UdmfMap();
  m.stackedRooms({ dx: STACK_DX });
  // [rc4l] A row only the stacked view can see, as WDI07's fan clouds stand beside a clocktower built off the town's side.
  for (let y = 160; y <= 352; y += 48) m.thing({ x: STACK_DX + 240, y, angle: 0, type: THING.TechPillar });
  m.thing({ x: 64, y: 256, angle: 0, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "up-east", at: { x: 64, y: 256, z: 0, angle: 0, pitch: -30 } }];

// [rc4l] The row: 5.04 while only the player's walk's sprites were kept, 0.72 with the stacked walk's.
export const BOXES = { MAP01: { things: { box: [0.3, 0.1, 0.7, 0.5], ceiling: 2 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "stacksprite", wad: buildStackSpriteWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
