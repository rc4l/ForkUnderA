// [rc4l] modelstandalone's room at screenblocks 8, GL against Vulkan: exit 0 when Vulkan frames the view inside the view window above the status bar as GL's SetViewport does, rather than drawing the whole screen's view behind the border.
//
// usage:  node scenarios/viewwindow.mjs [port]
import { runScenarioIfMain } from "../src/template.mjs";
import { buildModelStandaloneWad, VANTAGES } from "./modelstandalone.mjs";

// [rc4l] The two halves of the view window at 1280x720 (x 128..1152, rows 62..542), where the box ahead and the walls behind it land.
export const BOXES = { MAP01: {
  left: { box: [0.11, 0.1, 0.5, 0.74], ceiling: 3 },
  right: { box: [0.5, 0.1, 0.89, 0.74], ceiling: 3 },
} };

const SMALL = ["screenblocks 8"];
export const SIDES = [{ name: "gl", gl: true, commands: SMALL }, { name: "vk", commands: SMALL }];

runScenarioIfMain(import.meta.url, () => ({ name: "viewwindow", wad: buildModelStandaloneWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
