// [rc4l] The missing-upper map in GL with its wall cache on against off: exit 0 when the cache leaves GL's missing-texture fill as stock GL draws it.
//
// usage:  node scenarios/missingfillcache.mjs [port]
import { runScenarioIfMain } from "../src/template.mjs";
import { buildMissingUpperWad, VANTAGES, BOXES } from "./missingupper.mjs";

// [rc4l] WDI18's attic in GL: a replayed seg never registered its missing upper, so the fill went undrawn and the real ceiling showed in lit patches beside black gaps.
export const SIDES = [{ name: "nocache", gl: true, cvars: { gl_wallcache: 0 } }, { name: "cache", gl: true }];

runScenarioIfMain(import.meta.url, () => ({ name: "missingfillcache", wad: buildMissingUpperWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
