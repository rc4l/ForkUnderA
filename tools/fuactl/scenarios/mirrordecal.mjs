// [rc4l] Decals glued to a line mirror, GL against Vulkan from one frame: exit 0 when Vulkan draws them over the mirror's surface, as GL's RenderMirrorSurface draws them after the sheen.
//
// usage:  node scenarios/mirrordecal.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareSameFrame } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";

// [rc4l] The Decal thing, which glues its decal to the wall it faces away from at spawn.
const DECAL_THING = 9200;

// [rc4l] A shaded scorch and an additive glow, big enough to fill a box.
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

// [rc4l] The north wall is the mirror; the two Decal things stand before it facing south, so each marks it.
export function buildMirrorDecalWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, (edge) => edge === "north"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Line_Mirror } }
    : { side: { texturemiddle: edge === "south" ? "BRICK1" : "COMPBLUE" } });
  m.thing({ x: 256, y: 200, angle: 90, type: 1 });
  m.thing({ x: 184, y: 496, height: 64, angle: 270, type: DECAL_THING, arg0: 1 });
  m.thing({ x: 328, y: 496, height: 104, angle: 270, type: DECAL_THING, arg0: 2 });
  return wad([{ name: "DECALDEF", data: DECALDEF }, ...mapLumps("MAP01", m)]);
}

// [rc4l] Square on to the mirror, and at an angle where a decal's depth bias has a slope to work with.
export const VANTAGES = [
  { name: "facing", at: { x: 256, y: 200, z: 0, angle: 90, pitch: 0 } },
  { name: "oblique", at: { x: 100, y: 260, z: 0, angle: 60, pitch: 0 } },
];

// [rc4l] The scorch left of centre, the glow right of it, and the mirror's whole face from the side.
export const BOXES = { MAP01: {
  scorch: { vantage: "facing", box: [0.25, 0.25, 0.5, 0.65], ceiling: 2 },
  glow: { vantage: "facing", box: [0.5, 0.15, 0.75, 0.55], ceiling: 2 },
  slanted: { vantage: "oblique", box: [0.2, 0.1, 0.9, 0.65], ceiling: 2 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "mirrordecal", wad: buildMirrorDecalWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }), compareSameFrame);
