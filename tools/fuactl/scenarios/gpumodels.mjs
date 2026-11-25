// [Doruk-Aksoy] Animated MD3 (indexed) and MD2 (consecutive vertices) models blending between two keyframes, GL against Vulkan from one frame: exit 0 when Vulkan's vertex shader blends and places them where GL's does.
//
// usage:  node scenarios/gpumodels.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { encodePNG } from "../src/pngcore.mjs";
import { md2, md3 } from "../src/quakemodel.mjs";
import { runScenarioIfMain, compareSameFrame } from "../src/template.mjs";

// [Doruk-Aksoy] A block 16 wide and 32 tall, then the same block with its top 24 to its side (a model's y, across a north-facing actor) and raised to 48, so a blend between them leans across the view.
const BOTTOM = [[0, 0, 0], [16, 0, 0], [16, 16, 0], [0, 16, 0]];
const FRAMES = [
  [...BOTTOM, ...BOTTOM.map(([x, y]) => [x, y, 32])],
  [...BOTTOM, ...BOTTOM.map(([x, y]) => [x, y + 24, 48])],
];
const TRIS = [[0, 2, 1], [0, 3, 2], [4, 5, 6], [4, 6, 7], [0, 1, 5], [0, 5, 4], [1, 2, 6], [1, 6, 5], [2, 3, 7], [2, 7, 6], [3, 0, 4], [3, 4, 7]];
// [Doruk-Aksoy] Each vertex's texture coordinate from where it stands, so the skin's checks run up the sides.
const UVS = FRAMES[0].map(([x, y, z]) => [(x + y) / 32, z / 32]);

// [Doruk-Aksoy] A 32 by 32 skin of red and yellow checks, 8 across, so a misplaced corner shows.
function skin() {
  const rgb = Buffer.alloc(32 * 32 * 3);
  for (let y = 0; y < 32; y++)
    for (let x = 0; x < 32; x++) {
      const at = (y * 32 + x) * 3, odd = ((x >> 3) + (y >> 3)) & 1;
      rgb[at] = 230; rgb[at + 1] = odd ? 210 : 40; rgb[at + 2] = 20;
    }
  return encodePNG(32, 32, rgb);
}

// [Doruk-Aksoy] Two actors cycling BAR1 A and B, a second each, which MODELDEF draws as keyframes 0 and 1 of their model, so any moment is partway between the two.
export const MODELS = [
  { name: "ZXAnimMd3", num: 20220, file: "anim.md3", at: { x: 400, y: 420 } },
  { name: "ZXAnimMd2", num: 20221, file: "anim.md2", at: { x: 600, y: 420 } },
];

function decorate() {
  return MODELS.map((m) => `Actor ${m.name} ${m.num}\n{\n  Radius 16\n  Height 32\n  +NOGRAVITY\n  +NOINTERACTION\n` +
    `  States\n  {\n  Spawn:\n    BAR1 A 35\n    BAR1 B 35\n    Loop\n  }\n}\n`).join("\n");
}

function modeldef() {
  return MODELS.map((m) => `Model ${m.name}\n{\n  Path "models/"\n  Model 0 "${m.file}"\n  Skin 0 "zxskin.png"\n  Scale 3.0 3.0 3.0\n` +
    "  FrameIndex BAR1 A 0 0\n  FrameIndex BAR1 B 0 1\n}\n").join("\n");
}

export function buildGpuModelsPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 224 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  for (const x of MODELS) m.thing({ ...x.at, angle: 90, type: x.num });
  m.thing({ x: 500, y: 150, angle: 90, type: 1 });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) },
    { name: "decorate.txt", data: decorate() },
    { name: "modeldef.txt", data: modeldef() },
    { name: "models/anim.md3", data: md3({ frames: FRAMES, tris: TRIS, uvs: UVS }) },
    { name: "models/anim.md2", data: md2({ frames: FRAMES, tris: TRIS, uvs: UVS.map(([u, v]) => [Math.round(u * 32), Math.round(v * 32)]), skinW: 32, skinH: 32 }) },
    { name: "models/zxskin.png", data: skin() },
  ]);
}

// [Doruk-Aksoy] From the south, both models ahead and a little below eye height.
export const VANTAGES = [{ name: "ahead", at: { x: 500, y: 150, z: 0, angle: 90, pitch: 5 } }];

// [Doruk-Aksoy] The MD3 on the left and the MD2 on the right.
export const BOXES = { MAP01: {
  md3: { box: [0.25, 0.3, 0.5, 0.62], ceiling: 1 },
  md2: { box: [0.5, 0.3, 0.75, 0.62], ceiling: 1 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "gpumodels", wad: buildGpuModelsPk3(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES }), compareSameFrame);
