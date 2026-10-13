// [rc4l] The first-person weapon of a player on a damaging floor, GL against Vulkan from one frame: exit 0 when Vulkan tints it from the bottom up as GL's DrawPSprite does.
//
// usage:  node scenarios/damagetintweapon.mjs [port]
import { runScenarioIfMain, compareSameFrame } from "../src/template.mjs";
import { buildDamageTintWad, VANTAGES } from "./damagetint.mjs";

// [rc4l] God mode off as damagetint turns it off, and the fullscreen HUD, which leaves the weapon's tinted bottom on screen.
export const COMMANDS = ["god", "r_drawplayersprites 1", "screenblocks 11"];

// [rc4l] The hand at the weapon's bottom edge, where the tint is full.
export const BOXES = { MAP01: { hand: { box: [0.45, 0.88, 0.56, 1.0], ceiling: 3 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "damagetintweapon", wad: buildDamageTintWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, commands: COMMANDS }), compareSameFrame);
