// [rc4l] More views than the backend's old fixed pools held, GL against Vulkan: exit 0 when Vulkan draws twenty stacked ceilings and thirty reflective platforms in one frame as GL does, past the eight and sixteen it used to stop at.
//
// usage:  node scenarios/viewpools.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { acs0, callSpecial, PCD } from "../src/acs.mjs";
import { runScenarioIfMain } from "../src/template.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";

const MIRROR_TAG = 7;
const UPPER_WALLS = ["BRICK1", "COMPBLUE", "STARTAN3", "STONE2", "SKIN2", "BROWN1", "GRAY1", "MARBLE1", "METAL", "PIPE2", "TEKWALL1", "WOOD1"];

// [rc4l] A `cols` by `rows` grid of `size` square sectors open to each other, walled round the outside; props(col, row) is each one's.
function grid(m, cols, rows, size, props) {
  const cells = [];
  for (let cy = 0; cy < rows; cy++) for (let cx = 0; cx < cols; cx++) cells[cy * cols + cx] = m.sector(props(cx, cy));
  const wall = { texturemiddle: "STARTAN2" };
  for (let cy = 0; cy < rows; cy++) for (let cx = 0; cx < cols; cx++) {
    const s = cells[cy * cols + cx], x0 = cx * size, y0 = cy * size, x1 = x0 + size, y1 = y0 + size;
    if (cx === 0) m.wall(x0, y0, x0, y1, s, wall);
    if (cy === 0) m.wall(x1, y0, x0, y0, s, wall);
    if (cy === rows - 1) m.wall(x0, y1, x1, y1, s, wall);
    else m.opening(x0, y1, x1, y1, s, cells[(cy + 1) * cols + cx], { texturebottom: "STARTAN2", texturetop: "STARTAN2" },
      { texturebottom: "STARTAN2", texturetop: "STARTAN2" });
    if (cx === cols - 1) m.wall(x1, y1, x1, y0, s, wall);
    else m.opening(x1, y1, x1, y0, s, cells[cy * cols + cx + 1], { texturebottom: "STARTAN2", texturetop: "STARTAN2" },
      { texturebottom: "STARTAN2", texturetop: "STARTAN2" });
  }
  return cells;
}

// [rc4l] MAP01: twenty ceilings, each stacked onto an upper room of its own walls far to the east.
function stacks() {
  const m = new UdmfMap();
  grid(m, 5, 4, 256, () => ({ heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 160 }));
  for (let i = 0; i < 20; i++) {
    const cx = (i % 5) * 256, cy = Math.floor(i / 5) * 256, ux = 4096 + 512 * i;
    m.room(ux, 0, ux + 256, 256, { heightfloor: 160, heightceiling: 640, texturefloor: "FLOOR4_8", textureceiling: "CEIL5_1",
      lightlevel: 255 }, () => ({ side: { texturemiddle: UPPER_WALLS[i % UPPER_WALLS.length] } }));
    m.thing({ x: cx + 64, y: cy + 64, angle: 0, type: THING.LowerStackLookOnly, id: 10 + i });
    m.thing({ x: ux + 64, y: 64, angle: 0, type: THING.UpperStackLookOnly, id: 10 + i });
  }
  m.thing({ x: 512, y: 384, angle: 90, type: 1 });
  return mapLumps("MAP01", m);
}

// [rc4l] MAP02: thirty reflective platforms in a plain room, each at a height of its own below the eye, as planemirror's floor reflects in GL.
function mirrors() {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 200 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (let i = 0; i < 30; i++) {
    const x0 = 160 + (i % 6) * 128, y0 = 192 + Math.floor(i / 6) * 160;
    m.block(x0, y0, x0 + 96, y0 + 96, { heightfloor: 1 + i, heightceiling: 256, texturefloor: "FLAT1", textureceiling: "CEIL3_5",
      lightlevel: 200, id: MIRROR_TAG }, room, () => ({ texturebottom: "COMPBLUE" }));
  }
  m.thing({ x: 512, y: 64, angle: 90, type: 1 });
  const script = acs0([...callSpecial(SPECIAL.Sector_SetPlaneReflection, [MIRROR_TAG, 160, 0]), PCD.TERMINATE]);
  return mapLumps("MAP02", m, { behavior: script });
}

export function buildViewPoolsWad() {
  return wad([...stacks(), ...mirrors()]);
}

// [rc4l] Up across the stacked ceilings from the south-west corner, and down across the reflective platforms from the south wall.
export const VANTAGES = [
  { name: "up", at: { x: 64, y: 64, z: 0, angle: 45, pitch: -20 } },
  { name: "down", at: { x: 512, y: 64, z: 0, angle: 90, pitch: 25 } },
];

// [rc4l] The band of far ceilings and the platforms, where the views past the old pools fall.
export const BOXES = {
  MAP01: { ceilings: { vantage: "up", box: [0.05, 0.45, 0.75, 0.72], ceiling: 3, block: 8 } },
  MAP02: { floors: { vantage: "down", box: [0.0, 0.12, 1.0, 0.42], ceiling: 3, block: 8 } },
};

runScenarioIfMain(import.meta.url, () => ({ name: "viewpools", wad: buildViewPoolsWad(), maps: ["MAP01", "MAP02"],
  vantages: VANTAGES, boxes: BOXES }));
