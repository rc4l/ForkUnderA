// [Doruk-Aksoy] GLDEFS texture shaders' fua_Uniform lines set by the fua_SetShaderUniform ACS calls on Vulkan, four wall pieces and a HUD image against solid oracles swapped in by SetLineTexture: exit 0 when the defaults draw, each kind lands, two blocks naming one set share it, the calls return what they should, a reread keeps them, a new level starts from GLDEFS and a savegame brings them back.
//
// usage:  node scenarios/hwshaderuniforms.mjs [port]
import { UdmfMap, wad, pk3, mapLumps } from "../src/udmf.mjs";
import { acsScripts, AcsScript, PCD, hudMessage } from "../src/acs.mjs";
import { runScenarioIfMain, compareSequence } from "../src/template.mjs";
import { shaderCalls } from "./acsshaders.mjs";
import { solid } from "./hwshaderparity.mjs";

// [Doruk-Aksoy] One set per kind; ZXUD names zxtint with no uniforms of its own and reads tint backwards, and tint.fp declares its uniform itself as a GLSL writer would.
export const GLDEFS = `HardwareShader Texture "ZXUA" { Shader "shaders/tint.fp" fua_Name "zxtint" fua_Uniform vec3 tint = 1.0 0.0 0.0 }
HardwareShader Texture "ZXUB" { Shader "shaders/mode.fp" fua_Name "zxmode" fua_Uniform int mode = 0 }
HardwareShader Texture "ZXUC" { Shader "shaders/vecs.fp" fua_Name "zxvec" fua_Uniform vec2 rg = 0.0 0.0 fua_Uniform vec4 bw = 0.0 0.0 1.0 1.0 }
HardwareShader Texture "ZXUD" { Shader "shaders/tintb.fp" fua_Name "zxtint" }
HardwareShader ZXHUDU { Shader "shaders/hud.fp" fua_Name "zxhud" fua_Uniform float hudlevel = 0.5 }
${["ZXUA", "ZXUB", "ZXUC", "ZXUD", "ZXORR", "ZXORG", "ZXORB", "ZXORY"].map((t) => `brightmap texture ${t} { map ZXWHITE }`).join("\n")}
`;

const texel = (rgba) => `vec4 ProcessTexel()\n{\n\treturn ${rgba};\n}\n`;
export const SHADERS = {
  tint: `uniform vec3 tint;\n${texel("vec4(tint, 1.0)")}`,
  tintb: texel("vec4(tint.bgr, 1.0)"),
  mode: texel("mode == 3 ? vec4(0.0, 0.0, 1.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0)"),
  vecs: texel("vec4(rg, bw.z * bw.w, 1.0)"),
  hud: texel("vec4(hudlevel, hudlevel, 0.0, 1.0)"),
};

// [Doruk-Aksoy] The north wall's four pieces by line id, and the HUD image's place.
const PIECES = ["ZXUA", "ZXUB", "ZXUC", "ZXUD"];
const HUD_AT = { x: 0.5, y: 0.2 };

// [Doruk-Aksoy] What each piece and the HUD image draw at GLDEFS' defaults, and once script 1 has set every uniform.
export const ORACLES = {
  defaults: { pieces: ["ZXORR", "ZXORG", "ZXORB", "ZXORB"], hud: "ZXHOH" },
  set: { pieces: ["ZXORG", "ZXORB", "ZXORY", "ZXORG"], hud: "ZXHOY" },
};

export const STRINGS = ["zxtint", "tint", "zxmode", "mode", "zxvec", "rg", "bw", "zxhud", "hudlevel", "nosuch", "A",
  ...PIECES, "ZXORR", "ZXORG", "ZXORB", "ZXORY", "ZXHUDU", "ZXHOH", "ZXHOY"];
export const { enable, uniform, enableResult, uniformResult } = shaderCalls(STRINGS);
const str = (name) => STRINGS.indexOf(name);

// [Doruk-Aksoy] The pieces' middles set to `textures` in order, and the HUD image drawn in `font`.
const show = (textures, font) => [
  ...textures.flatMap((t, i) => [PCD.PUSHNUMBER, i + 1, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 1, PCD.PUSHNUMBER, str(t), PCD.SETLINETEXTURE]),
  PCD.PUSHNUMBER, str(font), PCD.SETFONT, ...hudMessage(str("A"), { id: 1, hold: 0, ...HUD_AT }),
];

// [Doruk-Aksoy] Script 5: zxmode's mode 3 (the second piece blue) when every call returns what it should, a set's name refused for anything but a declared uniform, else 0 (green).
export function returnsScript() {
  const s = new AcsScript();
  const expect = (words, want) => s.emit(...words, PCD.PUSHNUMBER, want, PCD.EQ).jump(PCD.IFNOTGOTO, "fail");
  s.emit(...uniform("zxmode", "mode", [0], { int: true }));
  expect(enableResult("zxtint", false), 0);
  expect(enableResult("zxhud", true), 0);
  expect(uniformResult("zxtint", "nosuch", [1]), 0);
  expect(uniformResult("zxtint", "mode", [3], { int: true }), 0);
  expect(uniformResult("zxtint", "tint", [0, 1, 0]), 1);
  s.emit(...uniform("zxmode", "mode", [3], { int: true }), PCD.TERMINATE);
  s.label("fail").emit(PCD.TERMINATE);
  return s.words();
}

// [Doruk-Aksoy] The closed scripts the steps puke, script 5 first since its jumps count from the lump's first word, and the OPEN one drawing the shaded HUD image.
export const SCRIPTS = [
  { open: false, number: 5, code: returnsScript() },
  { open: false, number: 1, code: [...uniform("zxtint", "tint", [0, 1, 0]), ...uniform("zxmode", "mode", [3], { int: true }),
    ...uniform("zxvec", "rg", [1, 1]), ...uniform("zxvec", "bw", [0, 0, 0, 1]), ...uniform("zxhud", "hudlevel", [1]), PCD.TERMINATE] },
  { open: false, number: 10, code: [...show(ORACLES.defaults.pieces, ORACLES.defaults.hud), PCD.TERMINATE] },
  { open: false, number: 11, code: [...show(ORACLES.set.pieces, ORACLES.set.hud), PCD.TERMINATE] },
  { open: false, number: 12, code: [...show(PIECES, "ZXHUDU"), PCD.TERMINATE] },
  { open: true, number: 20, code: [...show(PIECES, "ZXHUDU"), PCD.TERMINATE] },
];

// [Doruk-Aksoy] A lit room whose north wall is four 128-unit pieces, lines 1 to 4, the player facing them from far enough to see all four.
export function buildHwShaderUniformsPk3() {
  const m = new UdmfMap();
  const s = m.sector({ heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 160 });
  const plain = { texturemiddle: "BROWN1" };
  m.wall(0, 0, 0, 512, s, plain);
  PIECES.forEach((t, i) => m.wall(i * 128, 512, (i + 1) * 128, 512, s, { texturemiddle: t }, { id: i + 1 }));
  m.wall(512, 512, 512, 0, s, plain);
  m.wall(512, 0, 0, 0, s, plain);
  m.thing({ x: 256, y: 140, angle: 90, type: 1 });
  const behavior = acsScripts(SCRIPTS, STRINGS);
  const grey = [160, 160, 160];
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior })) },
    { name: "gldefs.txt", data: GLDEFS },
    ...Object.entries(SHADERS).map(([name, src]) => ({ name: `shaders/${name}.fp`, data: src })),
    ...PIECES.map((t) => ({ name: `textures/${t}.png`, data: solid(grey) })),
    { name: "textures/ZXORR.png", data: solid([255, 0, 0]) },
    { name: "textures/ZXORG.png", data: solid([0, 255, 0]) },
    { name: "textures/ZXORB.png", data: solid([0, 0, 255]) },
    { name: "textures/ZXORY.png", data: solid([255, 255, 0]) },
    { name: "textures/ZXWHITE.png", data: solid([255, 255, 255]) },
    { name: "graphics/ZXHUDU.png", data: solid(grey) },
    { name: "graphics/ZXHOH.png", data: solid([128, 128, 0]) },
    { name: "graphics/ZXHOY.png", data: solid([255, 255, 0]) },
  ]);
}

export const VANTAGES = [{ name: "wall", at: { x: 256, y: 140, z: 0, angle: 90, pitch: 0 } }];

export const STEPS = [
  { name: "plain", commands: ["crosshair 0", "r_drawplayersprites 0", 1500] },
  { name: "refDefaults", commands: ["puke 10", 800] },
  { name: "defaults", commands: ["puke 12", 800] },
  { name: "refSet", commands: ["puke 11", 800] },
  { name: "set", commands: ["puke 12", "puke 1", 800] },
  { name: "returns", commands: ["puke 5", 800] },
  { name: "reread", commands: ["fua_postshaders_reread", 300, "puke 5", 800] },
  { name: "beforeSave", commands: ["save fuactl_hwshaderuniforms.zds", 1500] },
  { name: "newLevel", commands: ["map MAP01", 3000] },
  { name: "loaded", commands: ["load fuactl_hwshaderuniforms.zds", 3000] },
];

// [Doruk-Aksoy] Each piece's middle third, seen from the vantage, and the HUD image's middle.
const PIECE_BOXES = [[0.27, 0.34, 0.33, 0.5], [0.41, 0.34, 0.46, 0.5], [0.54, 0.34, 0.59, 0.5], [0.67, 0.34, 0.73, 0.5]];
const HUD_BOX = [0.49, 0.19, 0.51, 0.22];
const pieces = (name, ref, step) => Object.fromEntries(PIECE_BOXES.map((box, i) => [`${name}${PIECES[i]}`, { box, pair: [ref, step], ceiling: 2 }]));
export const BOXES = { MAP01: {
  ...pieces("defaults", "refDefaults", "defaults"),
  defaultsHud: { box: HUD_BOX, pair: ["refDefaults", "defaults"], ceiling: 2 },
  ...pieces("set", "refSet", "set"),
  setHud: { box: HUD_BOX, pair: ["refSet", "set"], ceiling: 2 },
  ...pieces("returns", "refSet", "returns"),
  ...pieces("reread", "refSet", "reread"),
  ...pieces("newLevel", "refDefaults", "newLevel"),
  newLevelHud: { box: HUD_BOX, pair: ["refDefaults", "newLevel"], ceiling: 2 },
  ...pieces("loaded", "refSet", "loaded"),
} };

runScenarioIfMain(import.meta.url, () => ({ name: "hwshaderuniforms", wad: buildHwShaderUniformsPk3(), maps: ["MAP01"],
  vantages: VANTAGES, steps: STEPS, boxes: BOXES }), compareSequence);
