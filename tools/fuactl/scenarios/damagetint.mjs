// [rc4l] A player on a damaging floor (features/damage-tint), GL against Vulkan from one frame: exit 0 when Vulkan tints the status bar face and the player's own reflection as GL does.
//
// usage:  node scenarios/damagetint.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareSameFrame } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";

// [rc4l] ZDoom's dDamage_Nukage, 5 a cycle, which the tint reads off the floor's own texture.
const NUKAGE = 71;

// [rc4l] A nukage room whose north wall is a mirror, so the player sees their own tinted sprite in it.
export function buildDamageTintWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "NUKAGE1", textureceiling: "CEIL3_5",
    lightlevel: 192, special: NUKAGE }, (edge) => edge === "north"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Line_Mirror } }
    : { side: { texturemiddle: "BRICK1" } });
  m.thing({ x: 256, y: 320, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "facing", at: { x: 256, y: 320, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The launch turns god mode on (play's CHEATS) and the tint leaves a protected player alone, so `god` turns it off again; the status bar face is drawn.
export const COMMANDS = ["god", "screenblocks 10"];

// [rc4l] The face's chin and the reflection's legs, where the gradient is strongest, each 6-8 apart before Vulkan tinted them; damagetintweapon takes the weapon.
export const BOXES = { MAP01: {
  face: { box: [0.47, 0.93, 0.53, 0.99], ceiling: 3 },
  reflection: { box: [0.483, 0.44, 0.517, 0.5], ceiling: 3 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "damagetint", wad: buildDamageTintWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, commands: COMMANDS }), compareSameFrame);
