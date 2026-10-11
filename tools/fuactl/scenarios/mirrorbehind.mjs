// [rc4l] A thing standing behind the player in a part of the room only the mirror shows, GL against Vulkan: exit 0 when Vulkan's reflection holds it as GL's does.
//
// usage:  node scenarios/mirrorbehind.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";

// [rc4l] The pillar stands on its own block, a subsector GL's walk from the player never visits, as WDI12's hand dryer hangs on the wall behind the washroom mirror's viewer.
export function buildMirrorBehindWad() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 192 }, (edge) => (edge === "north"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Line_Mirror } }
    : { side: { texturemiddle: edge === "south" ? "BRICK1" : "COMPBLUE" } }));
  m.block(96, 32, 160, 96, { heightfloor: 0, heightceiling: 192, texturefloor: "FLAT14", textureceiling: "CEIL3_5",
    lightlevel: 192 }, room);
  // [rc4l] Off to the left, so its reflection stands clear of the reflected player rather than behind them.
  m.thing({ x: 128, y: 64, angle: 90, type: THING.TechPillar });
  m.thing({ x: 256, y: 400, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Close to the mirror and facing it, so the block is well behind the player's view.
export const VANTAGES = [{ name: "facing", at: { x: 256, y: 400, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] Left of the reflected player, where the pillar's reflection stands.
export const BOXES = { MAP01: { pillar: { box: [0.33, 0.25, 0.45, 0.6], ceiling: 2 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "mirrorbehind", wad: buildMirrorBehindWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
