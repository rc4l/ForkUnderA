// [rc4l] skywall's rooms, the player spawning facing away from the sky wall and then turning to it, GL against Vulkan in standalone mode: exit 0 when Vulkan's sky above the wall still hides what stands behind it, made that frame rather than kept from GL's last walk.
//
// usage:  node scenarios/skymove.mjs [port]
import { runScenarioIfMain } from "../src/template.mjs";
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";

export function buildSkyMoveWad() {
  const m = new UdmfMap();
  m.room(0, 0, 256, 256, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.room(264, 0, 520, 256, { heightfloor: 0, heightceiling: 512, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "BROWN1" } }));
  // [rc4l] Facing west, so the frames GL walks before standalone takes over see no sky wall on the east side.
  m.thing({ x: 224, y: 128, angle: 180, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "turned", at: { x: 32, y: 128, z: 0, angle: 0, pitch: -30 } }];

// [rc4l] The sky above the near room's wall, where the tall room shows through a sky kept from the spawn view.
export const BOXES = { MAP01: { sky: { box: [0.1, 0.05, 0.9, 0.35], ceiling: 3 } } };

export const SIDES = [{ name: "gl", gl: true }, { name: "standalone", cvars: { fua_dg_standalone: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "skymove", wad: buildSkyMoveWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
