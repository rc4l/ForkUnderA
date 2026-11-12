// [Doruk-Aksoy] hwactormaterials.mjs's checks launched with the SPIR-V optimizer on (fua_dg_spirv_optimize 1): exit 0 when each actor's fua_Material uniforms still land once the optimizer strips what each program never reads.
//
// usage:  node scenarios/hwactormaterialsopt.mjs [port]
import { runScenarioIfMain, compareSequence } from "../src/template.mjs";
import { buildHwActorMaterialsPk3, VANTAGES, STEPS, BOXES } from "./hwactormaterials.mjs";

runScenarioIfMain(import.meta.url, () => ({ name: "hwactormaterialsopt", wad: buildHwActorMaterialsPk3(), maps: ["MAP01"],
  vantages: VANTAGES, steps: STEPS, boxes: BOXES, sides: [{ name: "vk", launchCvars: { fua_dg_spirv_optimize: 1 } }] }), compareSequence);
