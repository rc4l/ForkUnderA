// [rc4l] Decals the map glues to a wall, GL against Vulkan from one frame: exit 0 when Vulkan draws the marks GL draws, a shaded scorch and an additive glow.
//
// usage:  node scenarios/walldecal.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareSameFrame } from "../src/template.mjs";

// [rc4l] The Decal thing, which glues its decal to the wall behind it at spawn.
const DECAL_THING = 9200;

// [rc4l] Numbered so a Decal thing can name them, big enough to fill a box; one an alpha mask in black, one drawn additively.
const DECALDEF = `decal FuaScorch 1
{
\tpic SCORCH1
\tshade "00 00 00"
\tx-scale 2.0
\ty-scale 2.0
}
decal FuaGlow 2
{
\tpic BFGLITE1
\tadd 1.0
\tfullbright
\tx-scale 2.0
\ty-scale 2.0
}
`;

export function buildWallDecalWad() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 512, y: 300, angle: 90, type: 1 });
  // [rc4l] Facing south, so each shoots its decal north onto the far wall.
  m.thing({ x: 420, y: 1000, height: 100, angle: 270, type: DECAL_THING, arg0: 1 });
  m.thing({ x: 604, y: 1000, height: 140, angle: 270, type: DECAL_THING, arg0: 2 });
  return wad([{ name: "DECALDEF", data: DECALDEF }, ...mapLumps("MAP01", m)]);
}

// [rc4l] Square on to the marked wall, where a decal's depth bias has no slope to work with, and at 45 degrees, where coplanar surfaces fight.
export const VANTAGES = [
  { name: "marks", at: { x: 512, y: 560, z: 0, angle: 90, pitch: 0 } },
  { name: "oblique", at: { x: 150, y: 760, z: 0, angle: 45, pitch: 0 } },
];

export const BOXES = { MAP01: {
  scorch: { vantage: "marks", box: [0.2, 0.25, 0.5, 0.75], ceiling: 2 },
  glow: { vantage: "marks", box: [0.5, 0.2, 0.8, 0.7], ceiling: 2 },
  slanted: { vantage: "oblique", box: [0.25, 0.15, 0.85, 0.75], ceiling: 2 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "walldecal", wad: buildWallDecalWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }), compareSameFrame);
