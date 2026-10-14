// [rc4l] A 3D fog volume seen from outside under invulnerability, GL against Vulkan: exit 0 when Vulkan leaves out its untextured fog panels as GL does under a fixed colormap.
//
// usage:  node scenarios/fogwall3d.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { fixedColormapSides } from "./fixedcolormap.mjs";

const FOG_TAG = 7;
// [rc4l] Sector_Set3DFloor's type 3 (not solid) and flag 4, FF_FOG.
const NONSOLID = 3, FOG = 4;

// [rc4l] A room with a block of red fog standing in it, its control sector's fade colour the fog's.
export function buildFogWall3dWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 768, 768, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.block(256, 384, 512, 640, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192, id: FOG_TAG }, room);
  m.room(2000, 2000, 2064, 2064, { heightfloor: 32, heightceiling: 160, texturefloor: "FLAT1", textureceiling: "FLAT1",
    lightlevel: 160, fadecolor: 0xff2020 }, (edge) => (edge === "south"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Sector_Set3DFloor, arg0: FOG_TAG, arg1: NONSOLID, arg2: FOG, arg3: 255 } }
    : { side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 384, y: 128, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "fog", at: { x: 384, y: 128, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The fog block's near face, between 32 and 160 high, clear of the weapon and the paused frame's PAUSE.
export const BOXES = { MAP01: { fog: { box: [0.3, 0.3, 0.7, 0.6], ceiling: 3 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "fogwall3d", wad: buildFogWall3dWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: fixedColormapSides("InvulnerabilitySphere") }));
