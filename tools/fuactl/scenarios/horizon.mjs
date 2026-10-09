// [rc4l] Line_Horizon on a template map, GL against Vulkan at fixed vantages: exit 0 when every box is under its ceiling.
//
// usage:  node scenarios/horizon.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] A 1024 court with a sky ceiling whose north wall is a horizon line carrying a texture GL never draws.
function court(m, { beyond }) {
  const floor = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1",
    lightlevel: 192 };
  m.room(0, 0, 1024, 1024, floor, (edge) => edge === "north"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: 9 } }
    : { side: { texturemiddle: "BROWN1" } });
  m.thing({ x: 512, y: 724, angle: 90, type: 1 });
  // [rc4l] A tall bright room behind the line, which GL never reaches and the map-built mesh holds.
  if (beyond) {
    m.room(0, 1100, 1024, 2000, { heightfloor: 0, heightceiling: 1500, texturefloor: "FLAT14",
      textureceiling: "CEIL3_5", lightlevel: 255 }, () => ({ side: { texturemiddle: "COMPBLUE" } }));
  }
}

// [rc4l] MAP01's sky is a texture; MAP02's is a skybox room, since a SkyViewpoint with no TID is the level's default.
export function buildHorizonWad() {
  const a = new UdmfMap();
  court(a, { beyond: false });
  const b = new UdmfMap();
  court(b, { beyond: true });
  b.room(4000, 4000, 4512, 4512, { heightfloor: 0, heightceiling: 384, texturefloor: "FLOOR0_1",
    textureceiling: "CEIL5_1", lightlevel: 255 }, (edge) => ({ side: { texturemiddle:
      edge === "north" ? "COMPBLUE" : edge === "east" ? "BRICK1" : edge === "south" ? "STARTAN2" : "BROWN1" } }));
  b.thing({ x: 4256, y: 4256, height: 128, angle: 0, type: 9080 });
  return wad([...mapLumps("MAP01", a), ...mapLumps("MAP02", b)]);
}

// [rc4l] Sky above the line and the floor strip below eye level; MAP01 allows 16 because GL and Vulkan map the dome differently.
const BOXES = {
  MAP01: { sky: { box: [0.10, 0.08, 0.90, 0.46], ceiling: 16 }, horizon: { box: [0.10, 0.505, 0.90, 0.58], ceiling: 6 } },
  MAP02: { sky: { box: [0.10, 0.08, 0.90, 0.46], ceiling: 6 },  horizon: { box: [0.10, 0.505, 0.90, 0.58], ceiling: 6 } },
};
const VANTAGES = [
  { name: "ahead",   at: { x: 512, y: 724, z: 0, angle: 90, pitch: 0 } },
  { name: "oblique", at: { x: 300, y: 650, z: 0, angle: 70, pitch: 0 } },
];

runScenarioIfMain(import.meta.url, () => ({ name: "horizon", wad: buildHorizonWad(), maps: ["MAP01", "MAP02"],
  vantages: VANTAGES, boxes: BOXES }));
