// [rc4l] A double sky (MAPINFO doublesky), GL against Vulkan: exit 0 when Vulkan draws sky2 with sky1 over it as a second layer, its see-through texels showing sky2, as GL's sky portal does.
//
// usage:  node scenarios/skydouble.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [rc4l] A sky1 that is mostly see-through: an imp on nothing.
const TEXTURES = `texture TSKY, 256, 128
{
\tpatch TROOA1, 100, 30
}
`;

const MAPINFO = `map MAP01 "Double sky"
{
\tsky1 = "TSKY", 0
\tsky2 = "SKY3", 0
\tdoublesky
}
`;

export function buildSkyDoubleWad() {
  const m = new UdmfMap();
  m.room(0, 0, 768, 768, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1", lightlevel: 192 },
    () => ({ side: { texturemiddle: "BRICK1" } }));
  m.thing({ x: 384, y: 128, angle: 90, type: 1 });
  return wad([{ name: "MAPINFO", data: MAPINFO }, { name: "TEXTURES", data: TEXTURES }, ...mapLumps("MAP01", m)]);
}

// [rc4l] Looking up over the far wall, and turned 45 degrees, since a 256-wide sky repeats every 90.
export const VANTAGES = [
  { name: "north", at: { x: 384, y: 128, z: 0, angle: 90, pitch: -25 } },
  { name: "turned", at: { x: 384, y: 384, z: 0, angle: 135, pitch: -25 } },
];

export const BOXES = { MAP01: {
  north: { vantage: "north", box: [0.1, 0.05, 0.9, 0.4], ceiling: 3 },
  turned: { vantage: "turned", box: [0.1, 0.05, 0.9, 0.4], ceiling: 3 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "skydouble", wad: buildSkyDoubleWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
