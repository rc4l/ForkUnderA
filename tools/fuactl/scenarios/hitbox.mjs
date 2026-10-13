// [rc4l] The hitbox view (features/hitboxviz) over a few still decorations, GL against Vulkan from one frame: exit 0 when Vulkan draws each actor's collision box where GL's lines are.
//
// usage:  node scenarios/hitbox.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareSameFrame } from "../src/template.mjs";
import { THING } from "../src/doomnums.mjs";

// [rc4l] A lit room with pillars and columns at three distances, so the boxes cross the floor and stand against the far wall.
export function buildHitboxWad() {
  const m = new UdmfMap();
  m.room(0, 0, 768, 768, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 300, y: 300, angle: 90, type: THING.TechPillar });
  m.thing({ x: 470, y: 420, angle: 90, type: THING.Column });
  m.thing({ x: 240, y: 560, angle: 90, type: THING.HeartColumn });
  m.thing({ x: 520, y: 680, angle: 90, type: THING.BurningBarrel });
  m.thing({ x: 384, y: 96, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Square on, and from the side looking down a little, where an edge runs nearly toward the eye.
export const VANTAGES = [
  { name: "facing", at: { x: 384, y: 96, z: 0, angle: 90, pitch: 0 } },
  { name: "oblique", at: { x: 80, y: 200, z: 0, angle: 45, pitch: 12 } },
];

// [rc4l] The overlay on, offline, which needs nothing else.
export const COMMANDS = ["cl_fua_hitbox 1"];

// [rc4l] The near pillar's box, the far columns' and the edge of the player's own, each 7-18 apart before Vulkan drew them, and the whole room from the side.
export const BOXES = { MAP01: {
  pillar: { vantage: "facing", box: [0.29, 0.12, 0.39, 0.68], ceiling: 2 },
  columns: { vantage: "facing", box: [0.57, 0.47, 0.63, 0.62], ceiling: 2 },
  self: { vantage: "facing", box: [0.115, 0.0, 0.135, 0.95], ceiling: 2 },
  side: { vantage: "oblique", box: [0.1, 0.1, 0.9, 0.8], ceiling: 1 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "hitbox", wad: buildHitboxWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, commands: COMMANDS }), compareSameFrame);
