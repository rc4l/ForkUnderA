// [rc4l] skyboxopen's map with the scene at half scale against full (features/dynres): exit 0 when the skybox's view draws at half scale where it does at full.
//
// usage:  node scenarios/dynressky.mjs [port]
import { runScenarioIfMain, FULL_VS_HALF } from "../src/template.mjs";
import { buildSkyboxOpenWad, VANTAGES } from "./skyboxopen.mjs";

export const BOXES = { MAP01: {
  // [rc4l] 0.36 measured.
  sky: { box: [0.1, 0.05, 0.9, 0.4], ceiling: 1, floor: 0.1 }
} };

runScenarioIfMain(import.meta.url, () => ({ name: "dynressky", wad: buildSkyboxOpenWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: FULL_VS_HALF }));
