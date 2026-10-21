// [rc4l] mirrorbehind's map with the scene at half scale against full (features/dynres): exit 0 when the line mirror shows its reflection at half scale as at full, rather than the empty half of its picture.
//
// usage:  node scenarios/dynresmirror.mjs [port]
import { runScenarioIfMain, FULL_VS_HALF } from "../src/template.mjs";
import { buildMirrorBehindWad, VANTAGES } from "./mirrorbehind.mjs";

export const BOXES = { MAP01: {
  // [rc4l] 1.08 measured, and 21.8 while the reflection was flipped across the whole picture.
  reflection: { box: [0.2, 0.15, 0.8, 0.7], ceiling: 2.5, floor: 0.3 }
} };

runScenarioIfMain(import.meta.url, () => ({ name: "dynresmirror", wad: buildMirrorBehindWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: FULL_VS_HALF }));
