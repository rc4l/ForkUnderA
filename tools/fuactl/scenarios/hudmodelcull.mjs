// [rc4l] The HUD model while the player is partly invisible, GL against Vulkan: exit 0 when Vulkan culls the weapon model's faces as gl_RenderHUDModel does for a player not in STYLE_Normal, front faces counter-clockwise, and shades it as GL's weapon shadow.
//
// usage:  node scenarios/hudmodelcull.mjs [port]
import { runScenarioIfMain } from "../src/template.mjs";
import { kvxBox } from "../src/kvx.mjs";
import { buildHudModelPk3, VANTAGES } from "./hudmodel.mjs";

// [rc4l] The blur sphere turns the player fuzzy, and with it the weapon.
const COMMANDS = ["r_drawplayersprites 1", "r_fuzztype 0", "give BlurSphere"];
export const SIDES = [{ name: "gl", gl: true, commands: COMMANDS }, { name: "vk", commands: COMMANDS }];

// [rc4l] The post, held up the middle of the frame; culling the wrong side measures 6.4 here.
export const BOXES = { MAP01: { weapon: { box: [0.44, 0.40, 0.56, 0.78], ceiling: 3, block: 4 } } };

// [rc4l] One voxel column scaled up, whose mesh winds some of its triangles each way, so culling the wrong side shows.
runScenarioIfMain(import.meta.url, () => ({ name: "hudmodelcull", wad: buildHudModelPk3({ scale: "16.0 16.0 16.0", voxel: kvxBox(1, 1, 2) }), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
