// [Doruk-Aksoy] Three flat models overhead, translucent with MODELDEF's DONTCULLBACKFACES, translucent with no flag, and Normal with FORCECULLBACKFACES, each against a model whose culling is known, GL and Vulkan each against itself: exit 0 when the first draws whole, the second still culls and the third culls as a translucent one does.
//
// usage:  node scenarios/modelcullflags.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { kvxBox, KVX_TOP } from "../src/kvx.mjs";
import { encodePNG } from "../src/pngcore.mjs";
import { runScenarioIfMain, MUTE } from "../src/template.mjs";

// [Doruk-Aksoy] Translucent at full alpha draws as solid as Normal but is culled, so the two tell culling apart and nothing else.
export const ACTORS = [
  { name: "ZXDont", num: 20100, style: "Translucent", flag: "DONTCULLBACKFACES" },
  { name: "ZXPlain", num: 20101, style: "Translucent" },
  { name: "ZXForced", num: 20102, style: "Normal", flag: "FORCECULLBACKFACES" },
  { name: "ZXWhole", num: 20103, style: "Normal" },
];
const NUM = Object.fromEntries(ACTORS.map((a) => [a.name, a.num]));

function decorate() {
  return ACTORS.map((a) => `Actor ${a.name} ${a.num}\n{\n  RenderStyle ${a.style}\n  Alpha 1.0\n  +NOGRAVITY\n  +NOINTERACTION\n` +
    `  States\n  {\n  Spawn:\n    ELEC A -1\n    Stop\n  }\n}\n`).join("\n");
}

// [Doruk-Aksoy] Every actor gets the same 64-unit slab, with its own culling flag.
function modeldef() {
  return ACTORS.map((a) => `Model ${a.name}\n{\n  Path "models/"\n  Model 0 "flat.kvx"\n  Skin 0 "flatskin.png"\n  Scale 4.0 4.0 4.0\n` +
    (a.flag ? `  ${a.flag}\n` : "") + "  FrameIndex ELEC A 0 0\n}\n").join("\n");
}

// [Doruk-Aksoy] A lit room with three actors overhead, west to east.
function room(types) {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  types.forEach((type, i) => m.thing({ x: 128 + i * 128, y: 320, height: 128, angle: 90, type }));
  m.thing({ x: 256, y: 150, angle: 90, type: 1 });
  return m;
}

// [Doruk-Aksoy] MAP01 holds the flags, and MAP02 what each must look like in the same place: whole, whole (which a plain translucent slab must not match), and culled.
// [Doruk-Aksoy] One voxel thick with only its top face, each slab is a single flat face whose two triangles KVX's mesher winds opposite ways, so culling keeps half of it from either side.
export function buildModelCullFlagsPk3() {
  const skin = Buffer.alloc(16 * 16 * 3);
  for (let i = 0; i < 16 * 16; i++) { skin[i * 3] = 255; skin[i * 3 + 1] = 220; skin[i * 3 + 2] = 40; }
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", room([NUM.ZXDont, NUM.ZXPlain, NUM.ZXForced]))) },
    { name: "maps/MAP02.wad", data: wad(mapLumps("MAP02", room([NUM.ZXWhole, NUM.ZXWhole, NUM.ZXPlain]))) },
    { name: "decorate.txt", data: decorate() },
    { name: "modeldef.txt", data: modeldef() },
    { name: "models/flat.kvx", data: kvxBox(16, 16, 1, { faces: KVX_TOP }) },
    { name: "models/flatskin.png", data: encodePNG(16, 16, skin) },
  ]);
}

// [Doruk-Aksoy] Under the three slabs, looking up at their undersides.
export const VANTAGES = [{ name: "below", at: { x: 256, y: 150, z: 0, angle: 90, pitch: -30 } }];

// [Doruk-Aksoy] The three slabs, 0.16-0.34, 0.43-0.57 and 0.66-0.84 across and 0.46-0.57 down in both renderers.
const DONT_BOX = [0.15, 0.45, 0.35, 0.58], PLAIN_BOX = [0.42, 0.45, 0.58, 0.58], FORCED_BOX = [0.65, 0.45, 0.85, 0.58];
// [Doruk-Aksoy] Each renderer against its own MAP02: DONTCULLBACKFACES must match a Normal slab, a plain translucent one must not, and FORCECULLBACKFACES must match a culled one.
export const BOXES = { MAP01: Object.fromEntries(["gl", "vk"].flatMap((r) => [
  [`${r}Dont`, { box: DONT_BOX, pair: [r, `${r}Oracle`], ceiling: 1 }],
  [`${r}Plain`, { box: PLAIN_BOX, pair: [r, `${r}Oracle`], floor: 10 }],
  [`${r}Forced`, { box: FORCED_BOX, pair: [r, `${r}Oracle`], ceiling: 1 }],
])) };

export const SIDES = [{ name: "gl", gl: true, launchCvars: MUTE }, { name: "glOracle", gl: true, launchCvars: MUTE, commands: ["map MAP02"] },
  { name: "vk", launchCvars: MUTE }, { name: "vkOracle", launchCvars: MUTE, commands: ["map MAP02"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "modelcullflags", wad: buildModelCullFlagsPk3(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
