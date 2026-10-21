// [rc4l] pointlight's hall with the scene drawn at half scale (features/dynres), Vulkan with its light grid against without: exit 0 when the grid lights the half-scale scene exactly as no grid does, its tiles cut from the scene's pixels rather than the frame's.
//
// usage:  node scenarios/dynresgrid.mjs [port]
import { runScenarioIfMain } from "../src/template.mjs";
import { buildPointlightWad, VANTAGES, BOXES } from "./pointlight.mjs";

export const SIDES = [{ name: "flat", cvars: { fua_dg_clusters: 0, fua_dg_dynres_force: 0.5 } },
  { name: "clustered", cvars: { fua_dg_clusters: 1, fua_dg_dynres_force: 0.5 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "dynresgrid", wad: buildPointlightWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
