// [rc4l] A tall sky-roofed room seen level and looking up, GL against Vulkan: exit 0 when Vulkan frames its walls where GL does.
//
// usage:  node scenarios/pitchframing.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] GL draws the world 1.2 times taller than square (pixelstretch), so a wall top far above the eye is where an unstretched camera misses it most.
export function buildPitchFramingWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 128, heightceiling: 640, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 448, y: 448, angle: 225, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Facing the far corner from the near one, level and looking up, where the old camera put the corner's top 5 and 39 rows off GL's.
export const VANTAGES = [
  { name: "level", at: { x: 448, y: 448, z: 128, angle: 225, pitch: 0 } },
  { name: "up20",  at: { x: 448, y: 448, z: 128, angle: 225, pitch: -20 } },
  { name: "up40",  at: { x: 448, y: 448, z: 128, angle: 225, pitch: -40 } },
];

// [rc4l] Everything below the notify lines and above the weapon.
export const BOXES = { MAP01: { frame: { box: [0.0, 0.1, 1.0, 0.7], ceiling: 0.5 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "pitchframing", wad: buildPitchFramingWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
