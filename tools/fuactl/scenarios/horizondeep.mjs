// [rc4l] A horizon line far off across a sky-roofed court whose floor lies well under the eye, GL against Vulkan: exit 0 when Vulkan's floor strip meets its sky at the horizon as GL's does.
//
// usage:  node scenarios/horizondeep.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";

// [rc4l] The player stands on a ledge 256 over the court's floor, nearly 4000 from a line wide enough to fill the view, as on WDI13's ridge, where the sky quad running from the floor up fought the floor half below the eye.
export function buildHorizonDeepWad() {
  const m = new UdmfMap();
  const court = m.room(0, 0, 8192, 4096, { heightfloor: -256, heightceiling: 256, texturefloor: "FLOOR4_8",
    textureceiling: "F_SKY1", lightlevel: 192 }, (edge) => (edge === "north"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Line_Horizon } }
    : { side: { texturemiddle: "BROWN1" } }));
  m.block(4032, 64, 4160, 192, { heightfloor: 0, heightceiling: 256, texturefloor: "FLAT14", textureceiling: "F_SKY1",
    lightlevel: 192 }, court, () => ({ texturebottom: "BROWN1" }));
  m.thing({ x: 4096, y: 128, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Down the court at the line, and the same looking across it and down as WDI13's ridge does, where the window's depth varies along it.
export const VANTAGES = [{ name: "deep", at: { x: 4096, y: 128, z: 0, angle: 90, pitch: 0 } },
  { name: "oblique", at: { x: 4096, y: 128, z: 0, angle: 60, pitch: 12 } }];

// [rc4l] The floor strip from the horizon down, and across the oblique view the band where the old sky quad hatched into it.
export const BOXES = { MAP01: {
  strip: { box: [0.10, 0.505, 0.90, 0.62], ceiling: 6, vantage: "deep" },
  band: { box: [0.02, 0.34, 0.58, 0.40], ceiling: 4, vantage: "oblique" },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "horizondeep", wad: buildHorizonDeepWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
