// [rc4l] particles' red fountain, GL against Vulkan from one engine frame: exit 0 when Vulkan's particles take the fountain's colour as GL's do, rather than the particle texture's white.
//
// usage:  node scenarios/particlecolor.mjs [port]
import { runScenarioIfMain, compareSameFrame } from "../src/template.mjs";
import { buildParticlesWad } from "./particles.mjs";

// [rc4l] Close to the fountain, so its spray fills the middle of the frame.
export const VANTAGES = [{ name: "fountain", at: { x: 256, y: 300, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The spray: 2.73 with white particles against GL's red.
export const BOXES = { MAP01: { spray: { box: [0.4, 0.2, 0.6, 0.7], ceiling: 1 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "particlecolor", wad: buildParticlesWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }), compareSameFrame);
