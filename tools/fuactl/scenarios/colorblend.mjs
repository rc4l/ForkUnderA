// [rc4l] A fullbright additive sprite at full opacity, GL against Vulkan: exit 0 when Vulkan blends it by its own colour as r_usecolorblending makes GL do, colour squared onto the wall, not added at full strength.
//
// usage:  node scenarios/colorblend.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

const GLOW = 15001;

// [rc4l] One still frame of the imp's fireball, additive at alpha 1 and fullbright, as DoomImpBall flies, against a plain lit wall.
export function buildColorBlendWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 160 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 384, height: 40, angle: 90, type: GLOW });
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  return wad([...mapLumps("MAP01", m),
    { name: "DECORATE", data: `actor ZXGlow ${GLOW}\n{\n  RenderStyle Add\n  Alpha 1.0\n  +NOGRAVITY\n  States\n  {\n  Spawn:\n    BAL1 A -1 Bright\n    Stop\n  }\n}\n` }]);
}

export const VANTAGES = [{ name: "facing", at: { x: 256, y: 128, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The fireball, against the far wall: 0.66 blended by its colour, 9.00 when Vulkan added it at full strength.
export const BOXES = { MAP01: { glow: { box: [0.48, 0.46, 0.52, 0.52], ceiling: 3 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "colorblend", wad: buildColorBlendWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
