// [rc4l] A foggy sector open to the sky with MAPINFO's skyfog, GL against Vulkan from one frame: exit 0 when Vulkan hazes the sky in the sector's fade as GL does.
//
// usage:  node scenarios/skyfog.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareSameFrame } from "../src/template.mjs";

// [rc4l] A strong skyfog, so a missing or wrongly coloured layer is far past a box's ceiling.
const MAPINFO = `map MAP01 "Sky fog"\n{\n\tsky1 = "SKY1", 0\n\tskyfog = 160\n}\n`;

export function buildSkyFogWad() {
  const m = new UdmfMap();
  m.room(0, 0, 2048, 2048, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1",
    lightlevel: 255, fadecolor: 0x3080c0 }, () => ({ side: { texturemiddle: "BROWN1" } }));
  m.thing({ x: 1024, y: 512, angle: 90, type: 1 });
  return wad([{ name: "MAPINFO", data: MAPINFO }, ...mapLumps("MAP01", m)]);
}

// [rc4l] Looking up over the far wall, so the top of the frame is sky.
export const VANTAGES = [{ name: "up", at: { x: 1024, y: 512, z: 0, angle: 90, pitch: -35 } }];

export const BOXES = { MAP01: { sky: { box: [0.1, 0.05, 0.9, 0.35], ceiling: 3 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "skyfog", wad: buildSkyFogWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }), compareSameFrame);
