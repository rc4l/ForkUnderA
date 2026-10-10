// [rc4l] A stacked ceiling looking up at a steep sloped floor in the room above, GL against Vulkan: exit 0 when Vulkan's stacked view keeps the slope, as GL's walk through it draws it.
//
// usage:  node scenarios/stackslope.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

const STACK_DX = 4096;
// [rc4l] Rising 3 units a unit eastward, as steep as WDI07's clocktower roof, whose normal is mostly sideways.
const SLOPE = 3, SLOPE_X0 = STACK_DX + 256;

export function buildStackSlopeWad() {
  const m = new UdmfMap();
  const { upper } = m.stackedRooms({ dx: STACK_DX });
  // [rc4l] The roof: z = 128 + SLOPE * (x - SLOPE_X0), written as UDMF's plane a*x + b*y + c*z + d = 0.
  m.block(SLOPE_X0, 128, SLOPE_X0 + 64, 384, { heightfloor: 128, heightceiling: 640, texturefloor: "FLAT14",
    textureceiling: "F_SKY1", lightlevel: 255, floorplane_a: -SLOPE, floorplane_b: 0, floorplane_c: 1,
    floorplane_d: SLOPE * SLOPE_X0 - 128 }, upper, () => ({ texturebottom: "STARTAN2" }));
  m.thing({ x: 64, y: 256, angle: 0, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Up through the stacked ceiling at the slope's face, as from WDI07's street under the clocktower.
export const VANTAGES = [{ name: "up-east", at: { x: 64, y: 256, z: 0, angle: 0, pitch: -30 } }];

// [rc4l] The slope's face: 26.33 while the stacked view culled it as a wall, 0.00 once placed by its subsector.
export const BOXES = { MAP01: { roof: { box: [0.3, 0.1, 0.7, 0.5], ceiling: 1 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "stackslope", wad: buildStackSlopeWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
