// [rc4l] A warp2 water floor beside a still one, Vulkan captured twice apart: exit 0 when the water moved and the dry floor did not.
//
// usage:  node scenarios/warpwater.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareOverTime } from "../src/template.mjs";

// [rc4l] A room with a pool of FWATER1, which ANIMDEFS makes a warp2 flat as wdiexsec's AZWATER3 is.
export function buildWarpwaterWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.block(256, 512, 768, 896, { heightfloor: -16, heightceiling: 256, texturefloor: "FWATER1", textureceiling: "CEIL3_5",
    lightlevel: 192 }, room, () => ({ texturebottom: "STARTAN2" }));
  m.thing({ x: 512, y: 128, angle: 90, type: 1 });
  return wad([...mapLumps("MAP01", m), { name: "ANIMDEFS", data: "warp2 flat FWATER1\n" }]);
}

// [rc4l] From the dry floor's edge, looking down into the pool just beyond it.
export const VANTAGES = [{ name: "pool", at: { x: 512, y: 420, z: 0, angle: 90, pitch: 20 } }];

// [rc4l] Per pixel, since a ripple shifts a fairly even texture without moving its block means; the dry floor in front must hold still.
export const BOXES = { MAP01: {
  water: { box: [0.35, 0.2, 0.65, 0.42], floor: 1, block: 1 },
  dry: { box: [0.3, 0.85, 0.7, 0.95], ceiling: 0.05, block: 1 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "warpwater", wad: buildWarpwaterWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, intervalMs: 600 }), compareOverTime);
