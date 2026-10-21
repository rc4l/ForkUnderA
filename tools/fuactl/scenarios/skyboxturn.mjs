// [rc4l] A sky yard over a skybox room with a different texture on each wall, the player spawning facing east and then turning west, GL against Vulkan in standalone mode: exit 0 when Vulkan's skybox shows its west wall as GL does, walked from the skybox that frame rather than kept from GL's last walk.
//
// usage:  node scenarios/skyboxturn.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

export function buildSkyboxTurnWad() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.skyboxRoom(4000, 0);
  // [rc4l] Facing east, so the frames GL walks before standalone takes over walk the skybox only through the east sky.
  m.thing({ x: 512, y: 512, angle: 0, type: 1 });
  return wad(mapLumps("MAP01", m));
}

export const VANTAGES = [{ name: "west", at: { x: 512, y: 512, z: 0, angle: 180, pitch: -25 } }];

// [rc4l] The sky above the yard's west wall, the skybox room's west wall in GL.
export const BOXES = { MAP01: { sky: { box: [0.1, 0.05, 0.9, 0.35], ceiling: 3 } } };

export const SIDES = [{ name: "gl", gl: true }, { name: "standalone", cvars: { fua_dg_standalone: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "skyboxturn", wad: buildSkyboxTurnWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
