// [Doruk-Aksoy] A model far wider than its actor, its centre out of view while it reaches into view, once past the view's edge and once through a wall from a room of its own, GL and Vulkan each against a map that differs only in that: exit 0 when both draw it.
//
// usage:  node scenarios/modelrenderradius.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { kvxBox } from "../src/kvx.mjs";
import { runScenarioIfMain, MUTE } from "../src/template.mjs";

export const BIG = { name: "ZXBig", num: 20200 };

// [Doruk-Aksoy] A 16-radius actor drawn as a 768-wide, 32-high slab of few voxels, well under the backend's 300k-vertex dynamic ceiling.
function decorate() {
  return `Actor ${BIG.name} ${BIG.num}\n{\n  Radius 16\n  Height 32\n  +NOGRAVITY\n  +NOINTERACTION\n` +
    `  States\n  {\n  Spawn:\n    ELEC A -1\n    Stop\n  }\n}\n`;
}

function modeldef() {
  return `Model ${BIG.name}\n{\n  Path "models/"\n  Model 0 "slab.kvx"\n  Scale 32.0 32.0 32.0\n  FrameIndex ELEC A 0 0\n}\n`;
}

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
const WALLS = () => ({ side: { texturemiddle: "STARTAN2" } });

// [Doruk-Aksoy] Where the slab's centre stands in each case.
export const EDGE_CENTRE = { x: 128, y: 512 };
export const WALL_CENTRE = { x: 4608, y: 700 };

// [Doruk-Aksoy] The edge case: a hall whose west end holds the slab's centre in a pad sector the view never reaches (MAP01), or in the hall itself (oracle); the wall case: a room of its own north of the player's, in MAP01 only; and no slab at all (empty).
export function room({ oracle = false, empty = false } = {}) {
  const m = new UdmfMap();
  const hall = m.room(0, 0, 2048, 1024, ROOM, WALLS);
  if (!oracle && !empty) m.block(64, 448, 192, 576, ROOM, hall);
  if (!empty) m.thing({ ...EDGE_CENTRE, angle: 0, type: BIG.num });
  m.room(4096, 0, 5120, 512, ROOM, WALLS);
  m.room(4096, 520, 5120, 1024, ROOM, WALLS);
  if (!oracle && !empty) m.thing({ ...WALL_CENTRE, angle: 0, type: BIG.num });
  m.thing({ x: 700, y: 200, angle: 60, type: 1 });
  return m;
}

export function buildModelRenderRadiusPk3() {
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", room())) },
    { name: "maps/MAP02.wad", data: wad(mapLumps("MAP02", room({ oracle: true }))) },
    { name: "maps/MAP03.wad", data: wad(mapLumps("MAP03", room({ empty: true }))) },
    { name: "decorate.txt", data: decorate() },
    { name: "modeldef.txt", data: modeldef() },
    { name: "models/slab.kvx", data: kvxBox(24, 24, 1) },
  ]);
}

// [Doruk-Aksoy] Over the slab's east end looking east and down, its centre's pad well behind the view, and looking north at the wall it pokes through.
export const VANTAGES = [
  { name: "edge", at: { x: 400, y: 512, z: 0, angle: 0, pitch: 30 } },
  { name: "wall", at: { x: 4608, y: 64, z: 0, angle: 90, pitch: 0 } },
];

const EDGE_BOX = [0.2, 0.3, 0.8, 0.9], WALL_BOX = [0.15, 0.52, 0.85, 0.64];
// [Doruk-Aksoy] Each renderer against its own maps: past the edge the slab draws as it does with its centre in the hall and is there at all (Vulkan's frustum dropped it in both), and through the wall it draws at all.
export const BOXES = { MAP01: Object.fromEntries(["gl", "vk"].flatMap((r) => [
  [`${r}Edge`, { box: EDGE_BOX, vantage: "edge", pair: [r, `${r}Oracle`], ceiling: 1 }],
  [`${r}EdgeDrawn`, { box: EDGE_BOX, vantage: "edge", pair: [r, `${r}Empty`], floor: 10 }],
  [`${r}Wall`, { box: WALL_BOX, vantage: "wall", pair: [r, `${r}Oracle`], floor: 10 }],
])) };

export const SIDES = ["gl", "vk"].flatMap((r) => [["", []], ["Oracle", ["map MAP02"]], ["Empty", ["map MAP03"]]].map(([suffix, commands]) =>
  ({ name: r + suffix, gl: r === "gl", launchCvars: MUTE, commands })));

runScenarioIfMain(import.meta.url, () => ({ name: "modelrenderradius", wad: buildModelRenderRadiusPk3(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
