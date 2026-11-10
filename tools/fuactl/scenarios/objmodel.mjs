// [Doruk-Aksoy] OBJ models through MODELDEF, an octagonal prism with textured quad sides and an eight-cornered cap beside files that are broken on purpose, GL against Vulkan: exit 0 when both draw the prism alike and the broken ones load without a crash.
//
// usage:  node scenarios/objmodel.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, MUTE } from "../src/template.mjs";

// [Doruk-Aksoy] An octagonal prism `radius` wide and `height` tall, Y up as OBJ has it: the bottom before any usemtl, eight quad sides in STARTAN2 and the top one eight-cornered face in BROWN1 by negative indices, with comments and CRLF.
export function prismObj(radius = 32, height = 96) {
  const lines = ["# octagonal prism", "o prism"];
  const ring = (y) => { for (let i = 0; i < 8; i++) { const a = (i * Math.PI) / 4; lines.push(`v ${(radius * Math.cos(a)).toFixed(4)} ${y} ${(radius * Math.sin(a)).toFixed(4)}`); } };
  ring(0);
  ring(height);
  for (let i = 0; i < 8; i++) lines.push(`vt ${i / 8} 0`, `vt ${i / 8} 1`);
  lines.push("vt 1 0", "vt 1 1", "vt 0.5 0.5");
  // [Doruk-Aksoy] Every face counter-clockwise seen from outside, as OBJ fronts are, so it draws the same with back faces culled (checked against UZDoom).
  lines.push(`f ${[1, 2, 3, 4, 5, 6, 7, 8].join(" ")}`);
  lines.push("usemtl STARTAN2");
  for (let i = 0; i < 8; i++) {
    const j = (i + 1) % 8, ti = i * 2 + 1, tj = i === 7 ? 17 : (i + 1) * 2 + 1;
    lines.push(`f ${i + 1}/${ti} ${i + 9}/${ti + 1} ${j + 9}/${tj + 1} ${j + 1}/${tj}`);
  }
  lines.push("usemtl BROWN1", "s off");
  lines.push(`f ${[-1, -2, -3, -4, -5, -6, -7, -8].map((v) => `${v}/-1`).join(" ")}`);
  return lines.join("\r\n") + "\r\n";
}

// [Doruk-Aksoy] Files each broken a different way, none of which may crash the load or the draw.
export const BROKEN = {
  "outofrange.obj": "v 0 0 0\nv 32 0 0\nv 0 64 0\nusemtl BROWN1\nf 1 2 9\nf 1 2 3\nf 0 1 2\nf 1/7 2 3\n",
  "empty.obj": "",
  "garbage.obj": "\u0001ÿ not an obj\nf x y z\nv 1\n",
  "manymaterials.obj": "v 0 0 0\nv 16 0 0\nv 0 32 0\n" + Array.from({ length: 40 }, (_, i) => `usemtl BROWN1\nf 1 2 3\n# ${i}`).join("\n") + "\n",
};

export const ACTORS = [
  { name: "ZXObjPrism", num: 20220, file: "prism.obj" },
  ...Object.keys(BROKEN).map((file, i) => ({ name: `ZXObjBroken${i}`, num: 20221 + i, file })),
];

function decorate() {
  return ACTORS.map((a) => `Actor ${a.name} ${a.num}\n{\n  Radius 16\n  Height 64\n  +NOGRAVITY\n  +NOINTERACTION\n` +
    `  States\n  {\n  Spawn:\n    ELEC A -1\n    Stop\n  }\n}\n`).join("\n");
}

function modeldef() {
  return ACTORS.map((a) => `Model ${a.name}\n{\n  Path "models/"\n  Model 0 "${a.file}"\n  Scale 1.0 1.0 1.0\n  FrameIndex ELEC A 0 0\n}\n`).join("\n");
}

// [Doruk-Aksoy] The prism straight ahead, the broken ones in a row behind it and a pillar off to its side; MAP02 the same room with none of them.
export const PRISM_AT = { x: 512, y: 400 };
function room(withActors) {
  const m = new UdmfMap();
  const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
  const hall = m.room(0, 0, 1024, 1024, ROOM, () => ({ side: { texturemiddle: "BROWN96" } }));
  // [Doruk-Aksoy] A pillar to look down from, since a vantage stands on the floor.
  m.block(640, 268, 704, 332, { ...ROOM, heightfloor: 160 }, hall, () => ({ texturebottom: "BROWN96" }));
  if (withActors) {
    m.thing({ ...PRISM_AT, angle: 90, type: ACTORS[0].num });
    ACTORS.slice(1).forEach((a, i) => m.thing({ x: 300 + i * 140, y: 800, angle: 90, type: a.num }));
  }
  m.thing({ x: 512, y: 150, angle: 90, type: 1 });
  return m;
}

export function buildObjModelPk3() {
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", room(true))) },
    { name: "maps/MAP02.wad", data: wad(mapLumps("MAP02", room(false))) },
    { name: "decorate.txt", data: decorate() },
    { name: "modeldef.txt", data: modeldef() },
    { name: "models/prism.obj", data: prismObj() },
    ...Object.entries(BROKEN).map(([file, text]) => ({ name: `models/${file}`, data: Buffer.from(text, "latin1") })),
  ]);
}

// [Doruk-Aksoy] From the south at eye height at the prism's sides, and from the pillar down onto its eight-cornered cap.
export const VANTAGES = [
  { name: "ahead", at: { x: 512, y: 150, z: 0, angle: 90, pitch: 10 } },
  { name: "above", at: { x: 672, y: 300, z: 160, angle: 148, pitch: 40 } },
];

// [Doruk-Aksoy] The prism, 0.45-0.55 across and 0.20-0.51 down at eye height, and 0.45-0.55 across and 0.36-0.61 down from the pillar.
const BOX = { ahead: [0.44, 0.19, 0.56, 0.52], above: [0.44, 0.35, 0.56, 0.62] };
// [Doruk-Aksoy] Each view's prism GL against Vulkan, and each renderer's against the empty room so it is there at all.
export const BOXES = { MAP01: Object.fromEntries(Object.entries(BOX).flatMap(([vantage, box]) => [
  [`${vantage}Prism`, { box, vantage, pair: ["gl", "vk"], ceiling: 1 }],
  ...["gl", "vk"].map((r) => [`${vantage}${r}Drawn`, { box, vantage, pair: [r, `${r}Empty`], floor: 10 }]),
])) };

export const SIDES = ["gl", "vk"].flatMap((r) => [["", []], ["Empty", ["map MAP02"]]].map(([suffix, commands]) =>
  ({ name: r + suffix, gl: r === "gl", launchCvars: MUTE, commands })));

runScenarioIfMain(import.meta.url, () => ({ name: "objmodel", wad: buildObjModelPk3(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
