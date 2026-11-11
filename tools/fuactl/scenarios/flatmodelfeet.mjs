// [Doruk-Aksoy] Sprites standing on, under and through flat translucent models, Vulkan against the same room without them: exit 0 when a flat model hides only what is really beyond it, as a floor would.
//
// usage:  node scenarios/flatmodelfeet.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, MUTE } from "../src/template.mjs";

// [Doruk-Aksoy] A flat square `half` across each way at y 0 (OBJ is Y up), both faces, so a style that culls back faces still shows it from below.
export function discObj(half = 96) {
  const v = [[-half, -half], [half, -half], [half, half], [-half, half]].map(([x, z]) => `v ${x} 0 ${z}`);
  return ["# flat disc", "o disc", ...v, "vt 0 0", "vt 1 0", "vt 1 1", "vt 0 1", "usemtl COMPBLUE",
    "f 1/1 2/2 3/3 4/4", "f 4/4 3/3 2/2 1/1"].join("\r\n") + "\r\n";
}

// [Doruk-Aksoy] Each flat model: its style, its MODELDEF Zoffset and scale, and whether it floats.
export const DISCS = [
  { name: "ZXFlatDisc", num: 21911, style: "Translucent", alpha: 0.6, zoffset: 1, scale: 1 },
  { name: "ZXFlatBridge", num: 21912, style: "Translucent", alpha: 0.6, zoffset: 1, scale: 1, float: true },
  { name: "ZXFlatRing", num: 21913, style: "Translucent", alpha: 0.6, zoffset: 0, scale: 0.42, float: true },
  { name: "ZXFlatSolid", num: 21914, style: "Normal", alpha: 1, zoffset: 1, scale: 1 },
];
// [Doruk-Aksoy] A still monster put down by a spot at runtime, since a locked launch has sv_nomonsters and that skips monsters among the map's things.
export const MONSTER = { name: "ZXFlatStander", spot: "ZXFlatStanderSpot", num: 21910 };

function decorate() {
  const monster = `Actor ${MONSTER.spot} ${MONSTER.num}\n{\n  +NOGRAVITY\n  +NOINTERACTION\n` +
    `  States\n  {\n  Spawn:\n    TNT1 A 1 NoDelay A_SpawnItemEx("${MONSTER.name}")\n    Stop\n  }\n}\n` +
    `Actor ${MONSTER.name}\n{\n  Monster\n  Radius 20\n  Height 56\n  +NOGRAVITY\n` +
    `  States\n  {\n  Spawn:\n    POSS A -1\n    Stop\n  }\n}\n`;
  return [monster, ...DISCS.map((d) => `Actor ${d.name} ${d.num}\n{\n  Radius 8\n  Height 1\n  RenderStyle ${d.style}\n  Alpha ${d.alpha}\n` +
    `  +NOGRAVITY\n  +NOINTERACTION\n  States\n  {\n  Spawn:\n    ELEC A -1\n    Stop\n  }\n}\n`)].join("\n");
}

function modeldef() {
  return DISCS.map((d) => `Model ${d.name}\n{\n  Path "models/"\n  Model 0 "disc.obj"\n  Scale ${d.scale} ${d.scale} ${d.scale}\n` +
    `  Zoffset ${d.zoffset}\n  FrameIndex ELEC A 0 0\n}\n`).join("\n");
}

// [Doruk-Aksoy] FUAMODEL naming every disc a flat surface, which other engines never read.
export function fuamodel() {
  return ["// the discs the backend orders as floors and ceilings", ...DISCS.map((d) => `FlatSurface ${d.name}`)].join("\n") + "\n";
}

// [Doruk-Aksoy] Where each ingredient stands: a floor disc with monsters across it, a disc floating at 96 with monsters on it, a ring round a monster's waist, an opaque disc.
export const FLOOR_DISC = { x: 256, y: 640 };
export const BRIDGE = { x: 768, y: 640, z: 96 };
export const RING = { x: 512, y: 860, z: 24 };
export const SOLID_DISC = { x: 768, y: 250 };
const PILLAR = 112;

// [Doruk-Aksoy] The room, with or without the discs, its player starting at `start` (the parity maps start at each vantage).
export function flatRoom(withDiscs, start = { x: 256, y: 380, angle: 90 }) {
  const m = new UdmfMap();
  const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 208 };
  const hall = m.room(0, 0, 1024, 1024, ROOM, () => ({ side: { texturemiddle: "BROWN96" } }));
  // [Doruk-Aksoy] Pillars to look down from, since a vantage stands on the floor.
  m.block(400, 480, 448, 528, { ...ROOM, heightfloor: PILLAR }, hall, () => ({ texturebottom: "BROWN96" }));
  m.block(488, 700, 536, 748, { ...ROOM, heightfloor: PILLAR }, hall, () => ({ texturebottom: "BROWN96" }));
  const monster = (x, y, height = 0) => m.thing({ x, y, height, angle: 270, type: MONSTER.num });
  monster(FLOOR_DISC.x, FLOOR_DISC.y + 60);
  monster(FLOOR_DISC.x - 56, FLOOR_DISC.y + 50);
  monster(FLOOR_DISC.x, FLOOR_DISC.y);
  monster(FLOOR_DISC.x, FLOOR_DISC.y - 60);
  monster(BRIDGE.x, BRIDGE.y + 30, BRIDGE.z + 1);
  monster(BRIDGE.x, BRIDGE.y - 30, BRIDGE.z + 1);
  monster(RING.x, RING.y);
  monster(SOLID_DISC.x, SOLID_DISC.y + 50);
  monster(SOLID_DISC.x, SOLID_DISC.y);
  if (withDiscs) {
    const at = [FLOOR_DISC, BRIDGE, RING, SOLID_DISC];
    DISCS.forEach((d, i) => m.thing({ x: at[i].x, y: at[i].y, height: at[i].z || 0, angle: 0, type: d.num }));
  }
  m.thing({ x: start.x, y: start.y, angle: start.angle, type: 1 });
  return m;
}

// [Doruk-Aksoy] The pk3 with `maps` ({ name, data } wads) beside the actors and the model; the scenario's own holds the room with and without the discs.
export function buildFlatModelPk3(maps = [
  { name: "MAP01", data: wad(mapLumps("MAP01", flatRoom(true))) },
  { name: "MAP02", data: wad(mapLumps("MAP02", flatRoom(false))) },
]) {
  return pk3([
    ...maps.map((m) => ({ name: `maps/${m.name}.wad`, data: m.data })),
    { name: "decorate.txt", data: decorate() },
    { name: "modeldef.txt", data: modeldef() },
    { name: "fuamodel.txt", data: fuamodel() },
    { name: "models/disc.obj", data: discObj() },
  ]);
}

// [Doruk-Aksoy] The floor disc at eye level and from a pillar, the bridge from the floor under it, the ring from a pillar, the opaque disc at eye level.
export const VANTAGES = [
  { name: "floorEye", at: { x: 256, y: 420, z: 0, angle: 90, pitch: 12 } },
  { name: "floorAbove", at: { x: 424, y: 504, z: PILLAR, angle: 141, pitch: 35 } },
  { name: "bridgeBelow", at: { x: 768, y: 500, z: 0, angle: 90, pitch: -22 } },
  { name: "ringAbove", at: { x: 512, y: 724, z: PILLAR, angle: 90, pitch: 48 } },
  { name: "solidEye", at: { x: 768, y: 80, z: 0, angle: 90, pitch: 10 } },
];

// [Doruk-Aksoy] Each against the room without discs: a monster drawn over a disc matches it (the fixed build reads 5-7, one drawn under it 19-24), one seen through a disc does not (24 fixed, 4 when drawn over it).
const PAIR = ["vk", "vkEmpty"];
export const BOXES = { MAP01: {
  floorNear: { vantage: "floorAbove", box: [0.418, 0.4583, 0.4414, 0.5556], pair: PAIR, ceiling: 10 },
  floorMiddle: { vantage: "floorAbove", box: [0.4883, 0.4167, 0.5117, 0.4931], pair: PAIR, ceiling: 10 },
  floorFar: { vantage: "floorAbove", box: [0.5453, 0.3958, 0.5703, 0.4417], pair: PAIR, ceiling: 10 },
  ringUpperBody: { vantage: "ringAbove", box: [0.4805, 0.3542, 0.5234, 0.4167], pair: PAIR, ceiling: 12 },
  ringLegsThrough: { vantage: "ringAbove", box: [0.4859, 0.4514, 0.5133, 0.5], pair: PAIR, floor: 15 },
  bridgeThrough: { vantage: "bridgeBelow", box: [0.4688, 0.25, 0.5156, 0.3611], pair: PAIR, floor: 15 },
  solidLegs: { vantage: "solidEye", box: [0.4688, 0.4167, 0.5156, 0.5417], pair: PAIR, ceiling: 10 },
} };

export const SIDES = [
  { name: "vk", gl: false, launchCvars: MUTE, commands: [] },
  { name: "vkEmpty", gl: false, launchCvars: MUTE, commands: ["map MAP02"] },
];

runScenarioIfMain(import.meta.url, () => ({ name: "flatmodelfeet", wad: buildFlatModelPk3(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
