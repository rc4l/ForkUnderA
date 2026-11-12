// [Doruk-Aksoy] hwshaderuniforms.mjs's checks launched with the SPIR-V optimizer on (fua_dg_spirv_optimize 1): exit 0 when the texture shaders' fua_Uniform values still land once the optimizer strips what each program never reads.
//
// usage:  node scenarios/hwshaderuniformsopt.mjs [port]
import { runScenarioIfMain, compareSequence } from "../src/template.mjs";
import { buildHwShaderUniformsPk3, VANTAGES, STEPS, BOXES } from "./hwshaderuniforms.mjs";

runScenarioIfMain(import.meta.url, () => ({ name: "hwshaderuniformsopt", wad: buildHwShaderUniformsPk3(), maps: ["MAP01"],
  vantages: VANTAGES, steps: STEPS, boxes: BOXES, sides: [{ name: "vk", launchCvars: { fua_dg_spirv_optimize: 1 } }] }), compareSequence);
