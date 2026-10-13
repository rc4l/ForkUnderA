// [rc4l] A sprite in the Subtract style against a bright wall, GL against Vulkan: exit 0 when Vulkan takes the sprite away from the screen as GL's GL_FUNC_REVERSE_SUBTRACT does, not over it as alpha.
//
// usage:  node scenarios/subtract.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

const SUB_PILLAR = 15002;

export function buildSubtractWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 300, angle: 90, type: SUB_PILLAR });
  m.thing({ x: 256, y: 160, angle: 90, type: 1 });
  return wad([...mapLumps("MAP01", m),
    { name: "DECORATE", data: `actor ZXSubtractPillar : TechPillar ${SUB_PILLAR}\n{\n  RenderStyle Subtract\n  Alpha 1.0\n}\n` }]);
}

export const VANTAGES = [{ name: "facing", at: { x: 256, y: 160, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The pillar, low in the middle, as stencilsprite frames it.
export const BOXES = { MAP01: { pillar: { box: [0.45, 0.42, 0.55, 0.72], ceiling: 3 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "subtract", wad: buildSubtractWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
