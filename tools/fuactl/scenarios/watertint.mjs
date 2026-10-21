// [rc4l] A pool under 3D-floor water whose control sector a script tints blue after the level is baked, GL against Vulkan: exit 0 when Vulkan's pool floor takes the blue as GL's does.
//
// usage:  node scenarios/watertint.mjs [port]
import { wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, GL_VS_VULKAN } from "../src/template.mjs";
import { pool } from "./water3d.mjs";

// [rc4l] WDI19's pool: the colour arrived after the bake, and the sweep compared nothing a colour changes, so the floor under the water stayed grey.
export function buildWaterTintWad() {
  // [rc4l] Additive, as WDI19's is, so the grey floor under it shows its deep blue whole.
  const { m, behavior } = pool("puked", "FLOOR0_1", { flags: 64, alpha: 128, floor: "FLAT19", tint: [0, 48, 192] });
  return wad(mapLumps("MAP01", m, { behavior }));
}

// [rc4l] From the walkway's edge down through the water at the pool floor.
export const VANTAGES = [{ name: "down", at: { x: 512, y: 370, z: -42, angle: 90, pitch: 30 } }];

// [rc4l] The pool floor seen through the water.
export const BOXES = { MAP01: { floor: { box: [0.2, 0.4, 0.8, 0.9], ceiling: 4 } } };

// [rc4l] Each side tints the water once its level is up and baked, by running the closed script.
export const SIDES = GL_VS_VULKAN.map((side) => ({ ...side, commands: ["puke 2"] }));

runScenarioIfMain(import.meta.url, () => ({ name: "watertint", wad: buildWaterTintWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
