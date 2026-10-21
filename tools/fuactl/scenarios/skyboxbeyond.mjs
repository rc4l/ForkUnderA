// [rc4l] A skybox area with low walls and a tower in a sealed room beside it, GL against Vulkan: exit 0 when the sky over the skybox walls shows sky, as GL's walk from the skybox viewpoint draws it, and not the tower.
//
// usage:  node scenarios/skyboxbeyond.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { THING } from "../src/doomnums.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] AOW03 (queue #34): its skybox sits among the map's own rooms, and a tower beyond the skybox's low walls stood in Vulkan's sky, since the portal pass kept whole material batches near its camera rather than what GL's walk from there reached.
export function buildSkyboxBeyondWad() {
  const m = new UdmfMap();
  // [rc4l] The player's yard under the sky.
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 512, y: 512, angle: 0, type: 1 });
  // [rc4l] The skybox area: open to the sky over walls only 192 high, its camera low by the west wall looking east.
  m.room(4000, 0, 4512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR0_1", textureceiling: "F_SKY1", lightlevel: 255 },
    () => ({ side: { texturemiddle: "BROWN1" } }));
  m.thing({ x: 4064, y: 256, height: 64, angle: 0, type: THING.SkyViewpoint });
  // [rc4l] A sealed room east of it, nothing GL's walk from the skybox camera can enter, with a tower rising far above the skybox walls.
  const yard = m.room(4700, 0, 5500, 512, { heightfloor: 0, heightceiling: 1200, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.block(5000, 192, 5128, 320, { heightfloor: 1100, heightceiling: 1200, texturefloor: "FLAT5_3", textureceiling: "F_SKY1", lightlevel: 255 },
    yard, () => ({ texturebottom: "COMPBLUE" }));
  return wad(mapLumps("MAP01", m));
}

// [rc4l] Looking east and up, so the skybox camera's view over its east wall fills the upper middle of the frame.
export const VANTAGES = [{ name: "east", at: { x: 512, y: 512, z: 0, angle: 0, pitch: -25 } }];

// [rc4l] Where the tower stands in Vulkan's sky; GL has sky there.
export const BOXES = { MAP01: { tower: { box: [0.45, 0.05, 0.6, 0.45], ceiling: 4 } } };

runScenarioIfMain(import.meta.url, () => ({ name: "skyboxbeyond", wad: buildSkyboxBeyondWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
