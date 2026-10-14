// [rc4l] A stencil-style actor in front of the player, GL against Vulkan: exit 0 when Vulkan draws it as GL's TM_MASK does, a flat silhouette in its stencil colour and GL's framed sprite texture's exact outline.
//
// usage:  node scenarios/stencilsprite.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

const STENCIL_PILLAR = 15000;

// [rc4l] A tech pillar drawn as a green stencil, against a plain wall so the silhouette stands out.
export function buildStencilSpriteWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 300, angle: 90, type: STENCIL_PILLAR });
  m.thing({ x: 256, y: 160, angle: 90, type: 1 });
  return wad([...mapLumps("MAP01", m),
    { name: "DECORATE", data: `actor ZXStencilPillar : TechPillar ${STENCIL_PILLAR}\n{\n  RenderStyle Stencil\n  StencilColor "00 ff 00"\n}\n` }]);
}

export const VANTAGES = [{ name: "facing", at: { x: 256, y: 160, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The pillar, low in the middle: 33.47 with its texture tinted green, 3.92 with the silhouette a texel too wide, unframed.
export const BOXES = { MAP01: { pillar: { box: [0.45, 0.42, 0.55, 0.72], ceiling: 3 } } };

// [rc4l] GL's captured sprite and the derived one, which standalone draws.
export const SIDES = [{ name: "gl", gl: true }, { name: "vk", gl: false }, { name: "standalone", cvars: { fua_dg_standalone: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "stencilsprite", wad: buildStencilSpriteWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
