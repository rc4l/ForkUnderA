// [Doruk-Aksoy] hwshader's two mod shaders and a third that does not compile, GL against Vulkan: exit 0 when GL launches instead of ending on a fatal error, draws the broken shader's wall plainly as Vulkan does, and still runs the other two.
//
// usage:  node scenarios/hwshaderbroken.mjs [port]
import { runScenarioIfMain, MUTE } from "../src/template.mjs";
import { buildHwShaderPk3, VANTAGES as SHADED, BOXES as SHADED_BOXES } from "./hwshader.mjs";

export const VANTAGES = [...SHADED, { name: "west", at: { x: 256, y: 256, z: 0, angle: 180, pitch: 0 } }];

// [Doruk-Aksoy] hwshader's two shaded walls, and the broken one's wall drawn as its plain texture.
export const BOXES = { MAP01: { ...SHADED_BOXES.MAP01, broken: { vantage: "west", box: [0.3, 0.2, 0.7, 0.6], ceiling: 2 } } };

export const SIDES = [{ name: "gl", gl: true, launchCvars: MUTE }, { name: "vk", launchCvars: MUTE }];

runScenarioIfMain(import.meta.url, () => ({ name: "hwshaderbroken", wad: buildHwShaderPk3({ broken: true }), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
