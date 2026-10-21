// [rc4l] mirrornested's two facing mirrors with Vulkan's reflections ray traced, GL against Vulkan: exit 0 when the traced reflection shows the player and the column GL's walks of each reflection draw, not the room alone.
//
// usage:  node scenarios/rtsprites.mjs [port]
import { runScenarioIfMain } from "../src/template.mjs";
import { buildMirrorNestedWad, VANTAGES } from "./mirrornested.mjs";

// [rc4l] Tracing is chosen when the scene is uploaded, so the Vulkan side uploads it again once it is on; no dynamic lights, which a traced reflection does not draw, so the column's glow is not judged.
export const SIDES = [
  { name: "gl", gl: true, commands: ["r_mirror_envmap 0", "gl_lights 0"] },
  { name: "vk", gl: false, commands: ["r_mirror_envmap 0", "gl_lights 0", "fua_dg_rtmirrors 1", "fua_diligent_scene"] },
];

// [rc4l] The player's first reflection, square in the middle, and the column's beside it.
export const BOXES = { MAP01: {
  player: { box: [0.42, 0.42, 0.58, 0.75], ceiling: 3, block: 8 },
  column: { box: [0.36, 0.47, 0.42, 0.62], ceiling: 3, block: 4 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "rtsprites", wad: buildMirrorNestedWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES, needs: ["raytracing"] }));
