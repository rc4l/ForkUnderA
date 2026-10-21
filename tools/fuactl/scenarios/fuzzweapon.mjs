// [rc4l] The weapon under partial invisibility with a r_fuzztype shader, GL against Vulkan at one pinned frame clock: exit 0 when Vulkan's 2D pass draws GL's fuzz shader on it.
//
// usage:  node scenarios/fuzzweapon.mjs [port]
import { runScenarioIfMain, GL_VS_VULKAN } from "../src/template.mjs";
import { buildFuzzWad, VANTAGES } from "./fuzz.mjs";

export const BOXES = { MAP01: { weapon: { box: [0.4, 0.78, 0.6, 1.0], ceiling: 3 } } };

const FUZZTYPE = Number(process.env.FUA_FUZZTYPE) || 5;
export const SIDES = GL_VS_VULKAN.map((s) => ({ ...s, cvars: { r_fuzztype: FUZZTYPE, fua_debug_frameclock: 4321 },
  commands: ["r_drawplayersprites 1", "give BlurSphere"] }));

runScenarioIfMain(import.meta.url, () => ({ name: "fuzzweapon", wad: buildFuzzWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
