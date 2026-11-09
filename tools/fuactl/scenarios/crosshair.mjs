// [Doruk-Aksoy] The triangle crosshair, GL and Vulkan: exit 0 when its outline is drawn and hollow, as Zandronum 3.3 draws it, and both renderers draw it alike.
//
// usage:  node scenarios/crosshair.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, MUTE } from "../src/template.mjs";

// [Doruk-Aksoy] A plain dark room, so a filled-in crosshair reads plainly against the wall behind it.
export function buildCrosshairWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 96 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "room", at: { x: 256, y: 64, z: 0, angle: 90, pitch: 0 } }];

// [Doruk-Aksoy] XHAIRB6 (15x6, left offset 7) at exactly 4x, white, over the middle of a 1280x720 view with no status bar or weapon.
const VIEW = { ...MUTE, screenblocks: 12, r_drawplayersprites: 0, crosshairscale: 0, cl_fua_crosshair_scale: 4,
  crosshairhealth: 0, crosshaircolor: "ffffff", crosshairgrow: 0 };
export const SIDES = [
  { name: "gl", gl: true, launchCvars: { ...VIEW, crosshair: 6 } },
  { name: "glnone", gl: true, launchCvars: { ...VIEW, crosshair: 0 } },
  { name: "vk", gl: false, launchCvars: { ...VIEW, crosshair: 6 } },
  { name: "vknone", gl: false, launchCvars: { ...VIEW, crosshair: 0 } },
];

// [Doruk-Aksoy] Pixel boxes of the 4x triangle drawn from (612, 360): inside its second and third rows, its base, and the whole of it.
const px = (x0, y0, x1, y1) => [x0 / 1280, y0 / 720, x1 / 1280, y1 / 720];
const HOLLOW = px(633, 369, 651, 375);
const BASE = px(616, 381, 656, 383);
const WHOLE = px(612, 360, 672, 384);

// [Doruk-Aksoy] Red-to-alpha read SmoothEdges' colour in the clear texels inside the outline, filling the triangle in on both renderers.
export const BOXES = { MAP01: {
  glHollow: { box: HOLLOW, pair: ["gl", "glnone"], ceiling: 3, block: 2 },
  vkHollow: { box: HOLLOW, pair: ["vk", "vknone"], ceiling: 3, block: 2 },
  glShown: { box: BASE, pair: ["gl", "glnone"], floor: 40, block: 2 },
  vkShown: { box: BASE, pair: ["vk", "vknone"], floor: 40, block: 2 },
  glVsVk: { box: WHOLE, pair: ["gl", "vk"], ceiling: 6, block: 4 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "crosshair", wad: buildCrosshairWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
