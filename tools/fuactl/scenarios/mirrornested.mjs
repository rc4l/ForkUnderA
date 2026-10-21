// [rc4l] Two mirrors facing across a short room, the second reflection large, GL against Vulkan: exit 0 when the reflection inside the reflection shows the column and the player as GL's walk of every reflection does, not the room alone.
//
// usage:  node scenarios/mirrornested.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, GL_VS_VULKAN } from "../src/template.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";

// [rc4l] A 256 square room, mirrors north and south, a lamp column (one frame, so separate launches agree) behind the player and to one side, near the south one.
export function buildMirrorNestedWad() {
  const m = new UdmfMap();
  m.room(0, 0, 256, 256, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    (edge) => (edge === "north" || edge === "south")
      ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Line_Mirror } }
      : { side: { texturemiddle: edge === "west" ? "BRICK1" : "COMPBLUE" } });
  m.thing({ x: 64, y: 48, angle: 90, type: THING.Column });
  m.thing({ x: 128, y: 200, angle: 90, type: 1 });
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Close to the north mirror, facing it.
export const VANTAGES = [{ name: "facing", at: { x: 128, y: 200, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] No sheen over the glass, so the reflections alone are compared.
export const SIDES = GL_VS_VULKAN.map((s) => ({ ...s, commands: ["r_mirror_envmap 0"] }));

// [rc4l] The column's second reflection, beside its first.
export const BOXES = { MAP01: { column: { box: [0.41, 0.37, 0.455, 0.62], ceiling: 3, block: 4 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "mirrornested", wad: buildMirrorNestedWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
