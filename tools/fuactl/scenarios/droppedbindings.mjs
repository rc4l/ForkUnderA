// [rc4l] The water3d pool with every binding dropped, as minimising the Vulkan window did, against one left alone: exit 0 when the translucent water comes back.
//
// usage:  node scenarios/droppedbindings.mjs [port]
import { wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { pool, VANTAGES } from "./water3d.mjs";

// [rc4l] A still water flat, because an animated one is re-bound by its next frame and hid the fault.
export function buildDroppedBindingsWad() {
  const { m, behavior } = pool("none", "GRNROCK");
  return wad(mapLumps("MAP01", m, { behavior }));
}

// [rc4l] The water's surface across the pool, which vanished to show the pool floor under it.
export const BOXES = { MAP01: { water: { box: [0.2, 0.6, 0.8, 0.68], ceiling: 0.05 } } };

// [rc4l] fua_dg_flushtextures drops the batches' bindings the way a minimise's swapchain resize does, without moving a window.
export const SIDES = [{ name: "kept" }, { name: "dropped", commands: ["fua_dg_flushtextures"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "droppedbindings", wad: buildDroppedBindingsWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
