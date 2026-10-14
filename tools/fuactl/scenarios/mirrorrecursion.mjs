// [rc4l] Two line mirrors facing each other, GL against Vulkan: exit 0 when Vulkan shows the mirror behind the player inside the one in front, reflections within reflections to r_mirror_recursions as GL draws them.
//
// usage:  node scenarios/mirrorrecursion.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";

// [rc4l] A corridor with a mirror at each end and coloured side walls, a column off to one side so each reflection has something in it.
export function buildMirrorRecursionWad() {
  const m = new UdmfMap();
  m.room(0, 0, 384, 768, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    (edge) => (edge === "north" || edge === "south")
      ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Line_Mirror } }
      : { side: { texturemiddle: edge === "west" ? "BRICK1" : "COMPBLUE" } });
  m.thing({ x: 96, y: 384, angle: 90, type: THING.Column });
  m.thing({ x: 192, y: 300, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Facing the north mirror from the middle of the corridor.
export const VANTAGES = [{ name: "facing", at: { x: 192, y: 300, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The middle of the north mirror, where the south one shows reflected (8 apart before Vulkan recursed, 3.6 now, the rest being the sprites deeper reflections lack), and the frame of reflections inside it.
export const BOXES = { MAP01: {
  inner: { box: [0.4, 0.3, 0.6, 0.6], ceiling: 4 },
  mirror: { box: [0.2, 0.15, 0.8, 0.7], ceiling: 3 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "mirrorrecursion", wad: buildMirrorRecursionWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
