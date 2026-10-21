// [rc4l] A raised-ceiling island whose uppers are left untextured, seen from inside it, GL against Vulkan: exit 0 when Vulkan draws the room's lower ceiling across the island as GL's missing-texture fill does.
//
// usage:  node scenarios/missingupper.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] WDI18's attic: the room round it is lower and no texture covers the step up, so GL closes the island at the room's ceiling and its own bright ceiling is never seen.
export function buildMissingUpperWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 160 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.block(256, 256, 768, 768, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "FLAT14",
    lightlevel: 255 }, room);
  m.thing({ x: 512, y: 400, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Inside the island, looking up.
export const VANTAGES = [{ name: "up", at: { x: 512, y: 400, z: 0, angle: 90, pitch: -50 } }];

// [rc4l] The ceiling over the island.
export const BOXES = { MAP01: { ceiling: { box: [0.2, 0.1, 0.8, 0.5], ceiling: 3 } } };

// [rc4l] GL with its wall cache off, as stock GL fills: builds before the cache refused to replay such segs skipped the fill in GL too.
export const SIDES = [{ name: "gl", gl: true, cvars: { gl_wallcache: 0 } }, { name: "vk", gl: false }];

runScenarioIfMain(import.meta.url, () => ({ name: "missingupper", wad: buildMissingUpperWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
