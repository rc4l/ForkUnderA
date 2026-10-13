// [rc4l] Decals glued to a 3D floor's side, GL against Vulkan from one frame: exit 0 when Vulkan draws them on the slab's face, as GL draws a wall's decals on each of its 3D floor blocks.
//
// usage:  node scenarios/ffdecal.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareSameFrame } from "../src/template.mjs";
import { SPECIAL } from "../src/doomnums.mjs";

const DECAL_THING = 9200;
const TARGET_TAG = 3;

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

// [rc4l] A solid STARTAN2 slab from 32 to 160 over the middle of a hall, its south face marked by two Decal things facing away from it.
export function buildFFDecalWad() {
  const m = new UdmfMap();
  const hall = m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8",
    textureceiling: "CEIL3_5", lightlevel: 192 }, () => ({ side: { texturemiddle: "BROWN1" } }));
  m.block(256, 384, 768, 768, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192, id: TARGET_TAG }, hall);
  m.room(2000, 2000, 2064, 2064, { heightfloor: 32, heightceiling: 160, texturefloor: "FLAT14", textureceiling: "FLAT14",
    lightlevel: 192 }, (edge) => (edge === "west"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Sector_Set3DFloor, arg0: TARGET_TAG, arg1: 1, arg2: 0, arg3: 255 } }
    : { side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 512, y: 128, angle: 90, type: 1 });
  m.thing({ x: 440, y: 368, height: 72, angle: 270, type: DECAL_THING, arg0: 1 });
  m.thing({ x: 584, y: 368, height: 120, angle: 270, type: DECAL_THING, arg0: 2 });
  return wad([{ name: "DECALDEF", data: DECALDEF }, ...mapLumps("MAP01", m)]);
}

// [rc4l] Square on to the slab's face, and at an angle.
export const VANTAGES = [
  { name: "facing", at: { x: 512, y: 128, z: 0, angle: 90, pitch: 0 } },
  { name: "oblique", at: { x: 300, y: 160, z: 0, angle: 60, pitch: 0 } },
];

// [rc4l] The scorch left of centre, the glow right of it, and the face from the side.
export const BOXES = { MAP01: {
  scorch: { vantage: "facing", box: [0.3, 0.3, 0.5, 0.65], ceiling: 2 },
  glow: { vantage: "facing", box: [0.5, 0.2, 0.7, 0.55], ceiling: 2 },
  slanted: { vantage: "oblique", box: [0.2, 0.2, 0.9, 0.65], ceiling: 2 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "ffdecal", wad: buildFFDecalWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }), compareSameFrame);
