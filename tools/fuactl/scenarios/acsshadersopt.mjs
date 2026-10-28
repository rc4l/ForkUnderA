// [Doruk-Aksoy] acsshaders.mjs's checks launched with the SPIR-V optimizer on (fua_dg_spirv_optimize 1), as WebGPU always runs: exit 0 when every check passes with programs the optimizer stripped of the input they never read.
//
// usage:  node scenarios/acsshadersopt.mjs [port]
import { runScenarioIfMain, compareSequence } from "../src/template.mjs";
import { buildAcsShadersPk3, VANTAGES, STEPS, BOXES } from "./acsshaders.mjs";

runScenarioIfMain(import.meta.url, () => ({ name: "acsshadersopt", wad: buildAcsShadersPk3(), maps: ["MAP01"],
  vantages: VANTAGES, steps: STEPS, boxes: BOXES, sides: [{ name: "vk", launchCvars: { fua_dg_spirv_optimize: 1 } }] }), compareSequence);
