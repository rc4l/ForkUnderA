// [rc4l] Skies other than the level's, GL against Vulkan: exit 0 when Vulkan shows a sky taken from a line (MBF's transfer) and a Sky2 sector's sky2 where GL does, beside the level's own.
//
// usage:  node scenarios/skylines.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

const STATIC_INIT = 190, INIT_TRANSFERSKY = 255, SKY2_SECTOR = 200, TRANSFER_TAG = 5;

// [rc4l] MAP01's sky1 is SKY1 and its sky2 SKY2.
const MAPINFO = `map MAP01 "Skies"
{
\tsky1 = "SKY1", 0
\tsky2 = "SKY2", 0
}
`;

// [rc4l] Room A takes the level's sky, B north of it the transfer line on A's south wall (upper SKY3) and C east of it a Sky2 sector, each with walls of its own since a sky shows above its sector's walls.
export function buildSkyLinesWad() {
  const m = new UdmfMap();
  const sky = { heightfloor: 0, heightceiling: 192, texturefloor: "FLOOR4_8", textureceiling: "F_SKY1", lightlevel: 192 };
  const a = m.sector({ ...sky });
  const b = m.sector({ ...sky, id: TRANSFER_TAG });
  const c = m.sector({ ...sky, special: SKY2_SECTOR });
  const wall = { texturemiddle: "BRICK1" };
  m.wall(0, 0, 0, 512, a, wall);
  m.opening(0, 512, 512, 512, a, b);
  m.opening(512, 512, 512, 0, a, c);
  m.wall(512, 0, 0, 0, a, { ...wall, texturetop: "SKY3" }, { special: STATIC_INIT, arg0: TRANSFER_TAG, arg1: INIT_TRANSFERSKY });
  m.wall(0, 512, 0, 1024, b, wall);
  m.wall(0, 1024, 512, 1024, b, wall);
  m.wall(512, 1024, 512, 512, b, wall);
  m.wall(512, 512, 1024, 512, c, wall);
  m.wall(1024, 512, 1024, 0, c, wall);
  m.wall(1024, 0, 512, 0, c, wall);
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  return wad([{ name: "MAPINFO", data: MAPINFO }, ...mapLumps("MAP01", m)]);
}

// [rc4l] Looking up over B's far wall, and over C's.
export const VANTAGES = [
  { name: "north", at: { x: 256, y: 128, z: 0, angle: 90, pitch: -25 } },
  { name: "east", at: { x: 128, y: 256, z: 0, angle: 0, pitch: -25 } },
];

// [rc4l] The sky above each far wall.
export const BOXES = { MAP01: {
  line: { vantage: "north", box: [0.3, 0.05, 0.7, 0.35], ceiling: 3 },
  sky2: { vantage: "east", box: [0.3, 0.05, 0.7, 0.35], ceiling: 3 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "skylines", wad: buildSkyLinesWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }));
