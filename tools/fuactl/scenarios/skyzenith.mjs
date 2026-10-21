// [rc4l] Straight up into a skybox sky no sky wall reaches, Vulkan's sky surfaces against the full-screen sky: exit 0 when every box is under its ceiling.
//
// usage:  node scenarios/skyzenith.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] A 4096 court open to a skybox sky, and the skybox room the sky is seen from.
export function buildSkyzenithWad() {
  const m = new UdmfMap();
  m.room(0, 0, 4096, 4096, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1",
    lightlevel: 192 }, () => ({ side: { texturemiddle: "BROWN1" } }));
  m.thing({ x: 2048, y: 2048, angle: 90, type: 1 });
  m.skyboxRoom(5000, 5000);
  return wad(mapLumps("MAP01", m));
}

// [rc4l] GL's sky walls stop at 32768, so from the middle of a 4096 court they leave a cone of about 3.6 degrees round the zenith.
export const VANTAGES = [
  { name: "zenith", at: { x: 2048, y: 2048, z: 0, angle: 90, pitch: -85 } },
  { name: "steep",  at: { x: 2048, y: 2048, z: 0, angle: 200, pitch: -75 } },
];

// [rc4l] Round the zenith, where both vantages put the uncovered cone; GL paints it with its full-screen sky, so both sides must too.
export const BOXES = { MAP01: { zenith: { box: [0.40, 0.25, 0.60, 0.55], ceiling: 0.05 } } };

// [rc4l] With no captured surface there, the surfaces side shows sky only if the background was painted first.
export const SIDES = [{ name: "background", cvars: { fua_mesh_sky: 0 } }, { name: "surfaces", cvars: { fua_mesh_sky: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "skyzenith", wad: buildSkyzenithWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
