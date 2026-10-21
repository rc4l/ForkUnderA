// [rc4l] A medkit on the floor of a pool under all-but-opaque water, against the same pool without it: exit 0 when the water hides it.
//
// usage:  node scenarios/underwatersprite.mjs [port]
import { wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { THING } from "../src/doomnums.mjs";
import { pool, VANTAGES } from "./waterseam.mjs";

// [rc4l] Alpha 254 still blends, so the water is in the sorted pass, yet shows nothing of what is under it.
const ALPHA = 254;

export function buildUnderwaterSpriteWad() {
  return wad([...mapLumps("MAP01", pool({ alpha: ALPHA, things: [{ x: 512, y: 600, type: THING.Medikit }] })),
    ...mapLumps("MAP02", pool({ alpha: ALPHA }))]);
}

// [rc4l] The water over the medkit, which the distance sort drew on top of the water.
export const BOXES = { MAP01: { medkit: { box: [0.45, 0.3, 0.55, 0.4], ceiling: 0.05, block: 4 } } };

// [rc4l] The empty pool is what the water must show, so the second side loads MAP02.
export const SIDES = [{ name: "medkit" }, { name: "empty", commands: ["map MAP02"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "underwatersprite", wad: buildUnderwaterSpriteWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
