// [rc4l] A long floor at a grazing angle under trilinear filtering and 16x anisotropy, GL against Vulkan: exit 0 when Vulkan's samplers take gl_texture_filter_anisotropic as GL's do.
//
// usage:  node scenarios/anisotropy.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, GL_VS_VULKAN } from "../src/template.mjs";

export function buildAnisotropyWad() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 4096, { heightfloor: 0, heightceiling: 256, texturefloor: "FLAT14", textureceiling: "CEIL3_5",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 512, y: 64, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Looking down the room, the floor running away to the far wall.
export const VANTAGES = [{ name: "floor", at: { x: 512, y: 64, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The far floor, where anisotropy keeps the tiles sharp and plain trilinear blurs them.
// [rc4l] 0.03 against GL with the samplers taking it, 0.44 when Vulkan kept its first filter and no anisotropy.
export const BOXES = { MAP01: { far: { box: [0.2, 0.52, 0.8, 0.66], ceiling: 0.2 } } };

export const SIDES = GL_VS_VULKAN.map((s) => ({ ...s, cvars: { gl_texture_filter: 4, gl_texture_filter_anisotropic: 16 } }));

runScenarioIfMain(import.meta.url, () => ({ name: "anisotropy", wad: buildAnisotropyWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
