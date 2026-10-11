// [rc4l] A recess sunk into a floor whose step walls have no lower texture, GL against Vulkan: exit 0 when GL fills the steps every frame as its missing-texture hack does, wall cache or not.
//
// usage:  node scenarios/missinglower.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] WDI18's rug: untextured steps whose fill GL registers inside GLWall::Process, which a replayed seg never runs, so the steps kept whatever an earlier frame left in those pixels.
export function buildMissingLowerWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR0_1", textureceiling: "CEIL3_5",
    lightlevel: 224 }, (edge) => ({ side: { texturemiddle: edge === "north" ? "REDWALL" : "STARTAN2" } }));
  m.block(384, 512, 640, 768, { heightfloor: -16, heightceiling: 256, texturefloor: "FLOOR0_1", textureceiling: "CEIL3_5",
    lightlevel: 224 }, room);
  m.thing({ x: 512, y: 360, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] First up at the red wall, so GL's framebuffer holds red, then down across the recess at its far step, where a missing fill shows that red.
export const VANTAGES = [{ name: "red", at: { x: 512, y: 900, z: 0, angle: 90, pitch: -20 } },
  { name: "down", at: { x: 512, y: 360, z: 0, angle: 90, pitch: 30 } }];

// [rc4l] The three steps that face the player.
export const BOXES = { MAP01: { steps: { box: [0.2, 0.14, 0.82, 0.31], ceiling: 2, vantage: "down" } } };

runScenarioIfMain(import.meta.url, () => ({ name: "missinglower", wad: buildMissingLowerWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
