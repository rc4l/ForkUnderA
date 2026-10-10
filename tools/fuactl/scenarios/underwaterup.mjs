// [rc4l] Looking up at a pool's water from under it, the water lit dark blue against lit like the pool: exit 0 when the surface's underside takes the pool's light either way.
//
// usage:  node scenarios/underwaterup.mjs [port]
import { wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { pool } from "./waterseam.mjs";

// [rc4l] GL lights a 3D floor's top plane from above it even seen from below, so the light under the water must not reach its underside.
export function buildUnderwaterUpWad() {
  return wad([...mapLumps("MAP01", pool({ light: 64, color: 0x3060ff })), ...mapLumps("MAP02", pool({ light: 152 }))]);
}

// [rc4l] On the pool floor, looking up and north at the surface and the walls rising out of it.
export const VANTAGES = [{ name: "up", at: { x: 512, y: 450, z: -154, angle: 90, pitch: -40 } }];

// [rc4l] The surface seen from below, with only what stands above the water behind it.
export const BOXES = { MAP01: { surface: { box: [0.25, 0.1, 0.75, 0.4], ceiling: 0.05 } } };

// [rc4l] The same pool with its water lit like the pool is what the underside must look like, so the second side loads MAP02.
export const SIDES = [{ name: "tinted" }, { name: "plain", commands: ["map MAP02"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "underwaterup", wad: buildUnderwaterUpWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
