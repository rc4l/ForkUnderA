// [rc4l] modelstandalone's voxel box ahead and its two reflections, GL against a launch with no GL at all (-vulkan): exit 0 when it renders them as GL does, rather than crashing on the model's GL vertex buffer.
//
// usage:  node scenarios/nogl.mjs [port]
import { runScenarioIfMain } from "../src/template.mjs";
import { buildModelStandaloneWad, VANTAGES, BOXES } from "./modelstandalone.mjs";

export const SIDES = [{ name: "gl", gl: true }, { name: "nogl", noGl: true }];

runScenarioIfMain(import.meta.url, () => ({ name: "nogl", wad: buildModelStandaloneWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
