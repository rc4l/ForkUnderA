// [rc4l] The camera-texture wall with Vulkan on a binding per material, GL against Vulkan: exit 0 when the screen shows the camera's picture, not the white placeholder its binding was made with before the first render.
//
// usage:  node scenarios/cameratexpermaterial.mjs [port]
import { runScenarioIfMain, GL_VS_VULKAN } from "../src/template.mjs";
import { buildCameraTexWad, VANTAGES, BOXES } from "./cameratex.mjs";

// [rc4l] The path WebGPU always takes, and Vulkan on a level with more materials than the array holds; set at launch, since the bindings the fault keeps are made at the level's upload.
export const SIDES = GL_VS_VULKAN.map((s) => (s.gl ? s : { ...s, launchCvars: { fua_dg_bindless: 0 } }));

runScenarioIfMain(import.meta.url, () => ({ name: "cameratexpermaterial", wad: buildCameraTexWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
