// [rc4l] A room whose sector is desaturated, GL against Vulkan: exit 0 when Vulkan draws it as grey as GL does, and a percentage written for the fraction is held at full grey rather than run past it.
//
// usage:  node scenarios/desaturate.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] COMPBLUE and BRICK1 are coloured enough that desaturating them is loud; MAP02 writes 99 as WDI09's sector 655 does.
function desaturatedRoom(desaturation) {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192, desaturation }, (edge) => ({ side: { texturemiddle: edge === "north" ? "COMPBLUE" : "BRICK1" } }));
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  return m;
}

export function buildDesaturateWad() {
  return wad([...mapLumps("MAP01", desaturatedRoom(0.99)), ...mapLumps("MAP02", desaturatedRoom(99))]);
}

// [rc4l] Facing the north wall across the room.
export const VANTAGES = [{ name: "wall", at: { x: 256, y: 128, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The wall's face, above the weapon.
export const BOXES = {
  MAP01: { wall: { box: [0.1, 0.15, 0.9, 0.6], ceiling: 2 } },
  MAP02: { wall: { box: [0.1, 0.15, 0.9, 0.6], ceiling: 2 } },
};

runScenarioIfMain(import.meta.url, () => ({ name: "desaturate", wad: buildDesaturateWad(), maps: ["MAP01", "MAP02"],
  vantages: VANTAGES, boxes: BOXES }));
