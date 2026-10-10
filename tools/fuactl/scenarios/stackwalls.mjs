// [rc4l] A tall stacked room beside a lower stacked annex with a plain pillar in it: exit 0 when Vulkan shows the stacked view over the annex as GL does.
//
// usage:  node scenarios/stackwalls.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { THING } from "../src/doomnums.mjs";

const STACK_TID = 7, STACK_DX = 4096;

// [rc4l] A ray from the tall room passes over the annex ABOVE its stacked ceiling, so no stacked plane is crossed and only GL's stack wall on the pillar (RENDERWALL_SECTORSTACK) catches it, as on WDI01's roofs.
export function buildStackWallsWad() {
  const m = new UdmfMap();
  const tall = m.room(0, 0, 1024, 512, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 128 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  // [rc4l] No upper texture on the tall room's side, so the gap between the two ceilings is open as WDI01's is.
  const annex = m.block(512, 64, 896, 448, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 128 }, tall);
  m.block(768, 192, 832, 320, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 128 }, annex);
  // [rc4l] Both stacked rooms open onto one bright room off the map's side.
  m.room(STACK_DX - 1024, -512, STACK_DX + 2048, 1024, { heightfloor: 0, heightceiling: 1024, texturefloor: "FLOOR4_8",
    textureceiling: "F_SKY1", lightlevel: 255 }, () => ({ side: { texturemiddle: "BRICK1" } }));
  m.thing({ x: 64, y: 64, angle: 0, type: THING.LowerStackLookOnly, id: STACK_TID });
  m.thing({ x: 544, y: 96, angle: 0, type: THING.LowerStackLookOnly, id: STACK_TID });
  m.thing({ x: STACK_DX + 64, y: 64, angle: 0, type: THING.UpperStackLookOnly, id: STACK_TID });
  m.thing({ x: 256, y: 256, angle: 0, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] From the tall room across the annex at the pillar, looking up so the rays clear the annex's ceiling.
export const VANTAGES = [{ name: "over-annex", at: { x: 256, y: 256, z: 0, angle: 0, pitch: -25 } }];

// [rc4l] The middle of the frame, over the annex and the pillar.
export const BOXES = { MAP01: { over: { box: [0.25, 0.15, 0.75, 0.6], ceiling: 1 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "stackwalls", wad: buildStackWallsWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
