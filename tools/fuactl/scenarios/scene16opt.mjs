// [Doruk-Aksoy] scene16.mjs's checks launched with the SPIR-V optimizer on (fua_dg_spirv_optimize 1): exit 0 when its shaders still read light past 1 and below 0 from the 16-bit scene.
//
// usage:  node scenarios/scene16opt.mjs [port]
import { runScenarioIfMain, compareSequence } from "../src/template.mjs";
import { buildScene16Pk3, VANTAGES, STEPS, BOXES } from "./scene16.mjs";

runScenarioIfMain(import.meta.url, () => ({ name: "scene16opt", wad: buildScene16Pk3(), maps: ["MAP01"],
  vantages: VANTAGES, steps: STEPS, boxes: BOXES, sides: [{ name: "vk", launchCvars: { fua_dg_scene16: 1, fua_dg_spirv_optimize: 1 } }] }), compareSequence);
