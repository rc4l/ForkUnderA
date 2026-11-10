// [Doruk-Aksoy] One model over 300k vertices and six that pass it only together, GL against Vulkan: exit 0 when Vulkan draws all five as GL does, rather than dropping what passed its fixed per-frame vertex ceiling.
//
// usage:  node scenarios/modelhighpoly.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { kvxBox } from "../src/kvx.mjs";
import { runScenarioIfMain, MUTE } from "../src/template.mjs";

// [Doruk-Aksoy] A voxel meshes twelve triangles a column, so 96 by 96 columns is 110,592 triangles (331,776 vertices) and 48 by 48 is 27,648 (82,944).
export const MODELS = [
  { name: "ZXHighPoly", num: 20210, file: "high.kvx", voxel: [96, 96, 4], color: [63, 8, 8], scale: 4 },
  { name: "ZXMidPoly", num: 20211, file: "mid.kvx", voxel: [48, 48, 4], color: [8, 63, 8], scale: 2 },
];

function decorate() {
  return MODELS.map((m) => `Actor ${m.name} ${m.num}\n{\n  Radius 16\n  Height 16\n  +NOGRAVITY\n  +NOINTERACTION\n` +
    `  States\n  {\n  Spawn:\n    ELEC A -1\n    Stop\n  }\n}\n`).join("\n");
}

function modeldef() {
  return MODELS.map((m) => `Model ${m.name}\n{\n  Path "models/"\n  Model 0 "${m.file}"\n  Scale ${m.scale}.0 ${m.scale}.0 ${m.scale}.0\n` +
    "  FrameIndex ELEC A 0 0\n}\n").join("\n");
}

// [Doruk-Aksoy] The big slab on the left and six blocks in two rows on the right, all ahead of the player.
export const HIGH_AT = { x: 300, y: 600 };
export const MID_AT = [[620, 520], [760, 520], [900, 520], [620, 680], [760, 680], [900, 680]];

export function buildModelHighPolyPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ ...HIGH_AT, angle: 90, type: MODELS[0].num });
  for (const [x, y] of MID_AT) m.thing({ x, y, angle: 90, type: MODELS[1].num });
  m.thing({ x: 512, y: 100, angle: 90, type: 1 });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) },
    { name: "decorate.txt", data: decorate() },
    { name: "modeldef.txt", data: modeldef() },
    ...MODELS.map((x) => ({ name: `models/${x.file}`, data: kvxBox(...x.voxel, { color: x.color }) })),
  ]);
}

// [Doruk-Aksoy] From the south wall looking north and a little down, every model in view.
export const VANTAGES = [{ name: "ahead", at: { x: 512, y: 100, z: 0, angle: 90, pitch: 15 } }];

// [Doruk-Aksoy] GL against Vulkan, 0.00 and 0.01 once fixed and 30 and 6.4 when Vulkan dropped them, over the slab, 0.0-0.49 across and 0.31-0.40 down, and over the six blocks, 0.54-0.95 across and 0.33-0.38 down.
export const BOXES = { MAP01: {
  high: { box: [0, 0.3, 0.5, 0.42], ceiling: 1 },
  mid: { box: [0.53, 0.32, 0.95, 0.39], ceiling: 1 },
} };

export const SIDES = [{ name: "gl", gl: true, launchCvars: MUTE }, { name: "vk", launchCvars: MUTE }];

runScenarioIfMain(import.meta.url, () => ({ name: "modelhighpoly", wad: buildModelHighPolyPk3(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
