// [Doruk-Aksoy] UZDoom's GLDEFS Material blocks on Vulkan, a map per case with a point light before its north wall, the SPIR-V optimizer off and on: exit 0 when normal and specular maps give highlights and PBR maps their own light, a shader that does not fit its texture's maps gives way to the built-in one, a Material block's own shader runs typed, a Material brightmap lights, and nothing changes without a dynamic light where UZDoom's specular model takes none.
//
// usage:  node scenarios/hwmaterials.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, MUTE } from "../src/template.mjs";
import { solid } from "./hwshaderparity.mjs";

const GREY = [160, 160, 160];

// [Doruk-Aksoy] A specular shader of its own, which reads the maps itself as UZDoom's func_spec.fp does and halves green and blue.
export const SHADERS = {
  red: "vec4 ProcessTexel()\n{\n\treturn vec4(1.0, 0.0, 0.0, 1.0);\n}\n",
  spectint: "void SetupMaterial(inout Material material)\n{\n\tSetMaterialProps(material, vTexCoord.st);\n\tmaterial.Base.rgb *= vec3(1.0, 0.5, 0.5);\n" +
    "\tmaterial.Specular = texture(speculartexture, vTexCoord.st).rgb;\n\tmaterial.Glossiness = uSpecularMaterial.x;\n\tmaterial.SpecularLevel = uSpecularMaterial.y;\n}\n",
};

const SPEC = "normal ZXNRM specular ZXWHT glossiness 4 specularlevel 1";
export const GLDEFS = `Material texture ZXMSPEC { ${SPEC} }
HardwareShader texture ZXMGATE { Shader "shaders/red.fp" }
Material texture ZXMGATE { ${SPEC} }
Material texture ZXMPBR { normal ZXNRM metallic ZXBLK roughness ZXRGH ao ZXWHT }
Material texture ZXMSH { ${SPEC} shader "shaders/spectint.fp" }
Material texture ZXMBRT { brightmap ZXWHT }
Material texture ZXMNOPE { normal ZXNRM }
`;

// [Doruk-Aksoy] Each map's north wall and whether a light stands before it, by map number.
export const CASES = [
  { wall: "ZXMSPEC", lit: true },   // MAP01: the built-in specular shader
  { wall: "ZXMGATE", lit: true },   // MAP02: a plain shader on a specular texture, which gives way to the built-in one
  { wall: "ZXMPLAIN", lit: true },  // MAP03: the same grey, no Material
  { wall: "ZXMPBR", lit: true },    // MAP04: the built-in PBR shader
  { wall: "ZXMSH", lit: true },     // MAP05: a Material block's own specular shader
  { wall: "ZXMSPEC", lit: false },  // MAP06: specular with no dynamic light
  { wall: "ZXMPLAIN", lit: false }, // MAP07: plain with no dynamic light
  { wall: "ZXMBRT", lit: false },   // MAP08: a Material brightmap, fully lit
  { wall: "ZXMNOPE", lit: true },   // MAP09: a normal map alone, the default type, as plain
];

const mapName = (n) => `MAP${String(n).padStart(2, "0")}`;

function room({ wall, lit }) {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 64 },
    (edge) => ({ side: { texturemiddle: edge === "north" ? wall : "ZXMPLAIN" } }));
  if (lit) m.thing({ x: 200, y: 448, height: 56, type: 9800, arg0: 255, arg1: 255, arg2: 255, arg3: 200 });
  m.thing({ x: 256, y: 200, angle: 90, type: 1 });
  return m;
}

export function buildHwMaterialsPk3() {
  return pk3([
    ...CASES.map((c, i) => ({ name: `maps/${mapName(i + 1)}.wad`, data: wad(mapLumps(mapName(i + 1), room(c))) })),
    { name: "gldefs.txt", data: GLDEFS },
    ...Object.entries(SHADERS).map(([name, src]) => ({ name: `shaders/${name}.fp`, data: src })),
    ...["ZXMSPEC", "ZXMGATE", "ZXMPLAIN", "ZXMPBR", "ZXMSH", "ZXMBRT", "ZXMNOPE"].map((t) => ({ name: `textures/${t}.png`, data: solid(GREY) })),
    { name: "textures/ZXNRM.png", data: solid([230, 128, 200]) },
    { name: "textures/ZXWHT.png", data: solid([255, 255, 255]) },
    { name: "textures/ZXBLK.png", data: solid([0, 0, 0]) },
    { name: "textures/ZXRGH.png", data: solid([128, 128, 128]) },
  ]);
}

export const VANTAGES = [{ name: "north", at: { x: 256, y: 200, z: 0, angle: 90, pitch: 0 } }];

// [Doruk-Aksoy] The wall above the weapon, where the light's highlight falls.
const WALL = [0.3, 0.25, 0.7, 0.45];
const RUNS = [["vk", { ...MUTE, fua_dg_spirv_optimize: 0 }], ["vkOpt", { ...MUTE, fua_dg_spirv_optimize: 1 }]];
export const SIDES = RUNS.flatMap(([r, cvars]) => CASES.map((c, i) => ({ name: `${r}${i + 1}`, launchCvars: cvars, commands: [`map ${mapName(i + 1)}`] })));

// [Doruk-Aksoy] Where UZDoom puts the highlight, the normal map tilting it toward the light's side, and the far side, which it leaves as plain (231 and 86 there, in either engine).
const HIGHLIGHT = [0.22, 0.38, 0.32, 0.52], FAR_SIDE = [0.6, 0.38, 0.7, 0.52];

// [Doruk-Aksoy] What each pair of maps must show: the same picture, or one standing apart.
const CHECKS = {
  gateGivesWay: { pair: [2, 1], ceiling: 1 },
  specularHighlights: { pair: [3, 1], floor: 8 },
  highlightOnLightsSide: { pair: [3, 1], floor: 100, box: HIGHLIGHT },
  farSideStaysPlain: { pair: [3, 1], ceiling: 2, box: FAR_SIDE },
  pbrOwnLight: { pair: [3, 4], floor: 8 },
  ownShaderRuns: { pair: [1, 5], floor: 8 },
  noLightNoHighlight: { pair: [7, 6], ceiling: 1 },
  brightmapLights: { pair: [7, 8], floor: 40 },
  defaultTypeIsPlain: { pair: [3, 9], ceiling: 1 },
};
export const BOXES = { MAP01: Object.fromEntries(RUNS.flatMap(([r]) => Object.entries(CHECKS).map(([name, c]) =>
  [`${r}_${name}`, { vantage: "north", box: c.box || WALL, pair: c.pair.map((n) => `${r}${n}`), ...(c.floor ? { floor: c.floor } : { ceiling: c.ceiling }) }]))) };

runScenarioIfMain(import.meta.url, () => ({ name: "hwmaterials", wad: buildHwMaterialsPk3(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
