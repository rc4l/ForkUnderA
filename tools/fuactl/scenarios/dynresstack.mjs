// [rc4l] stackportal's map with the scene at half scale against full (features/dynres): exit 0 when the view through the stacked ceiling draws at half scale where it does at full.
//
// usage:  node scenarios/dynresstack.mjs [port]
import { runScenarioIfMain, FULL_VS_HALF } from "../src/template.mjs";
import { buildStackPortalWad, VANTAGES } from "./stackportal.mjs";

export const BOXES = { MAP01: {
  // [rc4l] 0.34 and 0.63 measured.
  through: { box: [0.0, 0.05, 1.0, 0.4], ceiling: 1.5, floor: 0.1 }
} };

runScenarioIfMain(import.meta.url, () => ({ name: "dynresstack", wad: buildStackPortalWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: FULL_VS_HALF }));
