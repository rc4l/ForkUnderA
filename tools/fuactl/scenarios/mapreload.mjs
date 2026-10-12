// [rc4l] A level loaded a second time inside a Vulkan session, GL alone against GL's frame there: exit 0 when GL still draws the world after the level change.
//
// usage:  node scenarios/mapreload.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] Queue #41: a level change rebuilt the wall mesh's buffers, which left GL with no vertex array bound while its render state thought its own still was, so every world draw failed and the window kept the last frame (AoW's intermission).
export function buildMapReloadWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    (edge) => ({ side: { texturemiddle: edge === "north" ? "REDWALL" : "STARTAN2" } }));
  // [rc4l] Spawned facing away from the red wall, so a frame left over from before the reload cannot pass for the vantage.
  m.thing({ x: 256, y: 128, angle: 270, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "red", at: { x: 256, y: 200, z: 0, angle: 90, pitch: 0 } }];

export const BOXES = { MAP01: { red: { box: [0.2, 0.2, 0.8, 0.8], ceiling: 4 } } };

// [rc4l] GL alone is the reference; the second is GL's frame from a Vulkan session after the level loads again, which rebuilds the wall mesh GL draws through with fua_gl_meshdraw.
export const SIDES = [{ name: "gl", gl: true }, { name: "reload", frame: "gl", cvars: { fua_gl_meshdraw: 1 }, commands: ["map MAP01"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "mapreload", wad: buildMapReloadWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
