// [rc4l] Invulnerability's inverse colormap, GL against Vulkan: exit 0 when Vulkan greys and inverts the world, the sky, a sprite and the weapon as GL's main.fp does, a dark alcove and a bright room alike.
//
// usage:  node scenarios/fixedcolormap.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, GL_VS_VULKAN } from "../src/template.mjs";

const TECH_PILLAR = 48;
const MEDIKIT = 2012;

// [rc4l] A bright room with a dark sky-ceilinged alcove in it, a pillar before it and a medikit beside it: a fixed colormap ignores light, so the alcove must come out as bright as the room.
export function buildFixedColormapWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 224 }, (edge) => ({ side: { texturemiddle: edge === "north" ? "COMPBLUE" : "STARTAN2" } }));
  m.block(128, 288, 384, 448, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1", lightlevel: 80 }, room);
  m.thing({ x: 256, y: 240, angle: 90, type: TECH_PILLAR });
  // [rc4l] An item, which night vision (light amplification with r_enhanced_nightvision) shows inverted.
  m.thing({ x: 352, y: 208, angle: 90, type: MEDIKIT });
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Looking up past the pillar into the alcove, its sky and its dark floor in view.
export const VANTAGES = [{ name: "alcove", at: { x: 256, y: 64, z: 0, angle: 90, pitch: -12 } }];

// [rc4l] Each part a fixed colormap changes, with the weapon drawn, since GL draws it under the colormap too.
export function fixedColormapSides(power) {
  const commands = ["r_drawplayersprites 1", `give ${power}`];
  return GL_VS_VULKAN.map((s) => ({ ...s, commands }));
}

// [rc4l] At 1280x720, clear of the paused Vulkan frame's PAUSE: the sky over the alcove, its floor, the pillar, the weapon and the medikit (inverted under night vision), each at most 0.31 against GL and 16-184 before Vulkan had fixed colormaps.
export const BOXES = {
  MAP01: {
    sky: { box: [0.38, 0.21, 0.46, 0.35], ceiling: 3 },
    alcove: { box: [0.12, 0.42, 0.4, 0.62], ceiling: 3 },
    pillar: { box: [0.44, 0.3, 0.56, 0.75], ceiling: 3 },
    weapon: { box: [0.4, 0.78, 0.6, 1.0], ceiling: 3 },
    item: { box: [0.73, 0.8, 0.83, 0.94], ceiling: 3 },
  },
};

runScenarioIfMain(import.meta.url, () => ({ name: "fixedcolormap", wad: buildFixedColormapWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: fixedColormapSides("InvulnerabilitySphere") }));
