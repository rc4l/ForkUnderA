// [rc4l] A plain-sky room with a taller room behind its wall, GL against Vulkan: exit 0 when Vulkan's sky above the wall hides what stands behind it, as GL's sky walls (up to 32768) do.
//
// usage:  node scenarios/skywall.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

export function buildSkyWallWad() {
  const m = new UdmfMap();
  m.room(0, 0, 256, 256, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  // [rc4l] Closed off from the sky room and four times as tall, its far wall bright so it shows wherever the sky lets it through.
  m.room(264, 0, 520, 256, { heightfloor: 0, heightceiling: 512, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "BROWN1" } }));
  m.thing({ x: 32, y: 128, angle: 0, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "up", at: { x: 32, y: 128, z: 0, angle: 0, pitch: -30 } }];

// [rc4l] The sky above the near room's wall: 6.19 while it hid nothing, 0.27 with the occluders.
export const BOXES = { MAP01: { sky: { box: [0.1, 0.05, 0.9, 0.35], ceiling: 3 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "skywall", wad: buildSkyWallWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
