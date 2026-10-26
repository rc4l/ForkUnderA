// [Doruk-Aksoy] The HUD model with MODELDEF's FORCECULLBACKFACES while the player is in STYLE_Normal, against the same model with the player translucent, GL and Vulkan each against itself: exit 0 when the flag culls the weapon as a player not in STYLE_Normal does.
//
// usage:  node scenarios/hudforcecull.mjs [port]
import { runScenarioIfMain, MUTE } from "../src/template.mjs";
import { kvxBox } from "../src/kvx.mjs";
import { acs0, PCD } from "../src/acs.mjs";
import { buildHudModelPk3, VANTAGES } from "./hudmodel.mjs";

// [Doruk-Aksoy] Closed script 2, SetActorProperty(0, APROP_RenderStyle, STYLE_Translucent) on whoever pukes it: translucent at full alpha draws solid but culls the weapon.
const APROP_RENDERSTYLE = 4, ACS_STYLE_TRANSLUCENT = 64;
export const TRANSLUCENT_PLAYER = acs0([PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, APROP_RENDERSTYLE, PCD.PUSHNUMBER, ACS_STYLE_TRANSLUCENT,
  PCD.SETACTORPROPERTY, PCD.TERMINATE], [], { open: false, number: 2 });

// [Doruk-Aksoy] hudmodelcull's voxel column, whose mesh winds some of its triangles each way, with the flag on.
export function buildHudForceCullPk3() {
  return buildHudModelPk3({ scale: "16.0 16.0 16.0", voxel: kvxBox(1, 1, 2), flags: ["FORCECULLBACKFACES"], behavior: TRANSLUCENT_PLAYER });
}

// [Doruk-Aksoy] hudmodelcull's post, held up the middle of the frame.
const WEAPON_BOX = [0.44, 0.40, 0.56, 0.78];
export const BOXES = { MAP01: {
  gl: { box: WEAPON_BOX, pair: ["gl", "glTranslucent"], ceiling: 1, block: 4 },
  vk: { box: WEAPON_BOX, pair: ["vk", "vkTranslucent"], ceiling: 1, block: 4 },
} };

// [Doruk-Aksoy] The weapon shown again, since the harness hides it.
const WEAPON = ["r_drawplayersprites 1"];
export const SIDES = [{ name: "gl", gl: true, launchCvars: MUTE, commands: WEAPON },
  { name: "glTranslucent", gl: true, launchCvars: MUTE, commands: [...WEAPON, "puke 2"] },
  { name: "vk", launchCvars: MUTE, commands: WEAPON }, { name: "vkTranslucent", launchCvars: MUTE, commands: [...WEAPON, "puke 2"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "hudforcecull", wad: buildHudForceCullPk3(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
