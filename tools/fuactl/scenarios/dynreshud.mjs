// [rc4l] viewwindow's room at screenblocks 8 with the scene at half scale against full (features/dynres): exit 0 when the status bar and the border are the same pixels at both and the view inside the window is drawn below full scale where it stands at full.
//
// usage:  node scenarios/dynreshud.mjs [port]
import { runScenarioIfMain, FULL_VS_HALF } from "../src/template.mjs";
import { buildModelStandaloneWad, VANTAGES } from "./modelstandalone.mjs";

// [rc4l] At 1280x720 the status bar is the bottom 32 of 200 rows and the view window x 128..1152, rows 62..542, so the bar and the left border are 2D alone.
export const BOXES = { MAP01: {
  bar: { box: [0.0, 0.86, 1.0, 1.0], ceiling: 0.05 },
  border: { box: [0.0, 0.1, 0.09, 0.74], ceiling: 0.05 },
  // [rc4l] 0.89 measured: the floor proves the scene really was drawn below full scale, and the ceiling that it stands where it does at full.
  view: { box: [0.11, 0.1, 0.89, 0.74], ceiling: 2, floor: 0.3 },
} };

export const SIDES = FULL_VS_HALF.map((s) => ({ ...s, commands: ["screenblocks 8"] }));

runScenarioIfMain(import.meta.url, () => ({ name: "dynreshud", wad: buildModelStandaloneWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
