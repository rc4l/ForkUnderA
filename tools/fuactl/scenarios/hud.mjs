// [rc4l] The fullscreen HUD and the weapon, GL against Vulkan: exit 0 when Vulkan draws the health and ammo corners and the weapon in their colours, as GL's 2D does.
//
// usage:  node scenarios/hud.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, GL_VS_VULKAN } from "../src/template.mjs";

// [rc4l] A plain lit room; the HUD is what is being compared, the room is only what it is drawn over.
export function buildHudWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "room", at: { x: 256, y: 64, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The fullscreen HUD (screenblocks 11) with the weapon drawn.
export const SIDES = GL_VS_VULKAN.map((s) => ({ ...s, commands: ["r_drawplayersprites 1", "screenblocks 11"] }));

// [rc4l] At 1280x720, clear of the console lines and the paused Vulkan frame's PAUSE: the health icon and count, the ammo count and icon, and the weapon, which a HUD quad that took stack garbage for a fixed colormap drew solid black.
export const BOXES = {
  MAP01: {
    health: { box: [0.0, 0.95, 0.1, 1.0], ceiling: 3 },
    ammo: { box: [0.92, 0.95, 1.0, 1.0], ceiling: 3 },
    weapon: { box: [0.4, 0.78, 0.6, 1.0], ceiling: 3 },
  },
};

runScenarioIfMain(import.meta.url, () => ({ name: "hud", wad: buildHudWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
