// [Doruk-Aksoy] postprocess.mjs's checks launched with the SPIR-V optimizer on (fua_dg_spirv_optimize 1), as WebGPU always runs: exit 0 when every check passes with programs the optimizer stripped of what they never read, and again after it flips off and back on midway.
//
// usage:  node scenarios/postprocessopt.mjs [port]
import { runScenarioIfMain, compareSequence } from "../src/template.mjs";
import { buildPostProcessPk3, buildSteps, VANTAGES, BOXES } from "./postprocess.mjs";

export const STEPS = buildSteps(0, 1);

runScenarioIfMain(import.meta.url, () => ({ name: "postprocessopt", wad: buildPostProcessPk3(), maps: ["MAP01"],
  vantages: VANTAGES, steps: STEPS, boxes: BOXES, sides: [{ name: "vk", launchCvars: { fua_dg_spirv_optimize: 1 } }] }), compareSequence);
