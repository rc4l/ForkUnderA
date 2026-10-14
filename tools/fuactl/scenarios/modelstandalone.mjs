// [rc4l] A voxel model straight ahead and two more seen only in a line mirror, GL against Vulkan in standalone mode: exit 0 when standalone draws all three as GL does, rather than leaving solid models to a GL list it never draws.
//
// usage:  node scenarios/modelstandalone.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";

// [rc4l] A KVX box `sx` by `sy` by `sz` voxels in one palette colour, standing on its pivot; KVX carries its palette, so no skin is needed.
export function kvxBox(sx, sy, sz, rgb) {
  const column = 3 + sz;
  const offsetSize = (sx + 1) * 4 + sx * (sy + 1) * 2;
  const voxData = sx * sy * column;
  const numBytes = 24 + offsetSize + voxData;
  const out = Buffer.alloc(4 + numBytes + 768);
  let at = 0;
  const int = (v) => { out.writeInt32LE(v, at); at += 4; };
  int(numBytes);
  int(sx); int(sy); int(sz);
  // [rc4l] Pivots are 8.8 fixed point: the middle of the base, KVX's z running down.
  int(sx * 128); int(sy * 128); int(sz * 256);
  for (let x = 0; x <= sx; x++) int(offsetSize + x * sy * column);
  for (let x = 0; x < sx; x++) for (let y = 0; y <= sy; y++) { out.writeInt16LE(y * column, at); at += 2; }
  for (let x = 0; x < sx; x++) for (let y = 0; y < sy; y++) {
    out[at++] = 0; out[at++] = sz; out[at++] = 63;   // [rc4l] top, length, every face drawn
    for (let z = 0; z < sz; z++) out[at++] = 1;
  }
  // [rc4l] Palette entry 1 in KVX's six-bit colour.
  out[at + 3] = rgb[0] >> 2; out[at + 4] = rgb[1] >> 2; out[at + 5] = rgb[2] >> 2;
  return out;
}

// [rc4l] mirrorsprite's room with the tech pillar's sprite a red voxel box: one ahead of the player, two behind seen only in the north wall's mirror.
export function buildModelStandaloneWad() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 192 }, (edge) => edge === "north"
    ? { side: { texturemiddle: "STARTAN2" }, line: { special: SPECIAL.Line_Mirror } }
    : { side: { texturemiddle: edge === "south" ? "BRICK1" : "COMPBLUE" } });
  m.thing({ x: 256, y: 400, angle: 90, type: THING.TechPillar });
  for (const x of [128, 384]) m.thing({ x, y: 120, angle: 90, type: THING.TechPillar });
  m.thing({ x: 256, y: 250, angle: 90, type: 1 });
  return wad([...mapLumps("MAP01", m),
    { name: "VOXELDEF", data: 'ELECA = "ZXBOX" { }\n' },
    { name: "VX_START", data: "" }, { name: "ZXBOX", data: kvxBox(32, 32, 48, [255, 32, 32]) }, { name: "VX_END", data: "" }]);
}

export const VANTAGES = [{ name: "facing", at: { x: 256, y: 250, z: 0, angle: 90, pitch: 0 } }];

// [rc4l] The box ahead, low in the middle, and the mirror's two reflections either side of it: 33.69 ahead with none of them drawn.
export const BOXES = { MAP01: {
  ahead: { box: [0.42, 0.5, 0.58, 0.75], ceiling: 3 },
  mirrorLeft: { box: [0.405, 0.48, 0.448, 0.56], ceiling: 3 },
  mirrorRight: { box: [0.552, 0.48, 0.595, 0.56], ceiling: 3 },
} };

export const SIDES = [{ name: "gl", gl: true }, { name: "standalone", cvars: { fua_dg_standalone: 1 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "modelstandalone", wad: buildModelStandaloneWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
