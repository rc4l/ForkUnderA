// [rc4l] A pool under a translucent 3D-floor water surface, its walls above the water banded against unbanded: exit 0 when every box is under its ceiling.
//
// usage:  node scenarios/water3d.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { acs0, callSpecial, PCD } from "../src/acs.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";

// [rc4l] The tags of wdiexsec's pool: a swimmable 3D floor, and its control sector tinted blue by an OPEN script.
const POOL_TAG = 9, CONTROL_TAG = 5;

// [rc4l] A walkway round a deep pool, the water's control room, how the water's colour is set (by script, in the map, or not at all), and its flat.
export function pool(colour, water = "FWATER1") {
  const m = new UdmfMap();
  const walk = m.room(0, 0, 1024, 1024, { heightfloor: -42, heightceiling: 256, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 152 }, () => ({ side: { texturemiddle: "BROWN1" } }));
  m.block(256, 384, 768, 896, { heightfloor: -154, heightceiling: 256, texturefloor: "FLAT14", textureceiling: "CEIL3_5",
    lightlevel: 152, id: POOL_TAG }, walk, () => ({}), () => ({ texturebottom: "STARTAN2" }));
  // [rc4l] The control room's ceiling is the water's surface at -80, its light and colour are what the water casts below it, and COMPBLUE is the water's side.
  m.room(2000, 2000, 2064, 2064, { heightfloor: -194, heightceiling: -80, texturefloor: "FLAT1", textureceiling: water,
    lightlevel: 128, id: CONTROL_TAG, ...(colour === "map" ? { lightcolor: 0x64afff } : {}) }, (edge) => (edge === "south"
    ? { side: { texturemiddle: "COMPBLUE" }, line: { special: SPECIAL.Sector_Set3DFloor, arg0: POOL_TAG, arg1: 2, arg2: 0, arg3: 200 } }
    : { side: { texturemiddle: "COMPBLUE" } }));
  m.thing({ x: 512, y: 250, angle: 90, type: 1 });
  const behavior = colour === "script"
    ? acs0([...callSpecial(SPECIAL.Sector_SetColor, [CONTROL_TAG, 100, 175, 255]), PCD.TERMINATE]) : null;
  return { m, behavior };
}

// [rc4l] MAP01 tints the water by script as wdiexsec does, MAP02 in the map itself, MAP03 not at all.
export function buildWater3dWad() {
  const lumps = [];
  for (const [name, colour] of [["MAP01", "script"], ["MAP02", "map"], ["MAP03", "none"]]) {
    const { m, behavior } = pool(colour);
    lumps.push(...mapLumps(name, m, { behavior }));
  }
  return wad(lumps);
}

// [rc4l] From the walkway's south edge across the pool, at the far and side walls that rise out of the water to the walkway.
export const VANTAGES = [{ name: "across", at: { x: 512, y: 250, z: -42, angle: 90, pitch: 0 } }];

// [rc4l] The walls above the water, left and far, which took the water's light and blue; the water itself shows the walls below it and is left out.
export const BOXES = Object.fromEntries(["MAP01", "MAP02", "MAP03"].map((m) => [m, {
  left: { box: [0.0, 0.55, 0.18, 0.66], ceiling: 0.05 },
  far: { box: [0.36, 0.54, 0.64, 0.58], ceiling: 0.05 },
}]));

// [rc4l] Re-baked with the bands off, every wall takes its sector's own light, which is what the part above the water must have.
export const SIDES = ["0", "1"].map((bands) => ({ name: bands === "1" ? "banded" : "flat",
  cvars: { fua_surface_bands: bands }, commands: ["fua_mapbake_now"] }));

runScenarioIfMain(import.meta.url, () => ({ name: "water3d", wad: buildWater3dWad(), maps: ["MAP01", "MAP02", "MAP03"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
