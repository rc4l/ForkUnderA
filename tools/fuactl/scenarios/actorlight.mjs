// [rc4l] A sprite beside a red point light, GL against Vulkan: exit 0 when Vulkan lights the sprite with the light GL works out for its actor.
//
// usage:  node scenarios/actorlight.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { THING } from "../src/doomnums.mjs";

export function buildActorLightWad() {
  const m = new UdmfMap();
  m.room(0, 0, 768, 768, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 96 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 384, y: 160, angle: 90, type: 1 });
  // [rc4l] A tech pillar, which is not fullbright, and a PointLight (args red, green, blue, radius) just in front of it.
  m.thing({ x: 384, y: 448, angle: 0, type: THING.TechPillar });
  m.thing({ x: 384, y: 400, angle: 0, type: THING.PointLight, height: 48, arg0: 255, arg1: 0, arg2: 0, arg3: 160 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "column", at: { x: 384, y: 160, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The column, which GL tints red with its actor's light and Vulkan drew in the sector light alone.
export const BOXES = { MAP01: { column: { box: [0.475, 0.3, 0.525, 0.58], ceiling: 5, block: 8 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "actorlight", wad: buildActorLightWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
