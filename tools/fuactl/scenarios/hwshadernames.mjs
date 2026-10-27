// [Doruk-Aksoy] GLDEFS shaders reading GL main.fp's names and Zandronum 3.3's (camerapos, gl_TextureMatrix), two of them defining one macro and one function differently, one on a model's skin reading its model matrix, and two on HUD images, the old Process(color) handed the HudMessage's alpha, each GL and Vulkan against itself: exit 0 when every shader runs and finds each name as GL's base shader gives it, which it shows by drawing magenta where a plain magenta texture draws the same.
//
// usage:  node scenarios/hwshadernames.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { kvxBox } from "../src/kvx.mjs";
import { encodePNG } from "../src/pngcore.mjs";
import { runScenarioIfMain, MUTE } from "../src/template.mjs";
import { acs0, hudMessage, PCD } from "../src/acs.mjs";

const MAGENTA = "vec4(1.0, 0.0, 1.0, 1.0)", GREEN = "vec4(0.0, 1.0, 0.0, 1.0)";

// [Doruk-Aksoy] On the north wall (y 512, 0 to 160 high), 3.3's declarations kept as DnD keeps them: the eye, the surface's place and depth, the texel both ways, the colour and an identity model matrix.
const NAMES = `uniform vec3 camerapos;
varying vec4 pixelpos;
uniform float timer;
vec4 ProcessTexel()
{
\tfloat d = distance(pixelpos.xyz, camerapos);
\tbool ok = d >= pixelpos.w * 0.99 && d <= pixelpos.w * 1.8;
\tok = ok && distance(camerapos, uCameraPos.xyz) < 0.01 && abs(uCameraPos.w) < 0.001;
\tok = ok && abs(pixelpos.z - 512.0) < 1.0 && pixelpos.y > -1.0 && pixelpos.y < 161.0;
\tok = ok && vColor.a > 0.99;
\tok = ok && texture(tex, vTexCoord.st).a > 0.5 && getTexel(vTexCoord.st).a > 0.5;
\tok = ok && abs(ModelMatrix[3].x) < 0.01 && abs(gl_TextureMatrix[7][0].x - 1.0) < 0.01;
\treturn ok ? ${MAGENTA} : ${GREEN};
}
`;

// [Doruk-Aksoy] DnD's FreezingPulse.fp and IceBolt.fp shape: the same macro and helper, defined differently, each right only in its own program.
const clash = (edge, test) => `#define EDGE ${edge}
float h1(float n) { return fract(sin(n * ${edge}) * 43758.5453); }
vec4 Process(vec4 color)
{
\treturn EDGE ${test} 0.5 && h1(1.0) >= 0.0 ? ${MAGENTA} : ${GREEN};
}
`;

// [Doruk-Aksoy] On the model's skin: its matrix through both names, placed at its actor (400, 256) on the floor, and its surface within reach of it.
const MODEL = `vec4 ProcessTexel()
{
\tvec3 o = gl_TextureMatrix[7][3].xyz;
\tbool ok = abs(o.x - 400.0) < 2.0 && abs(o.z - 256.0) < 2.0 && abs(o.y) < 2.0;
\tok = ok && distance(ModelMatrix[3], gl_TextureMatrix[7][3]) < 0.01 && length(ModelMatrix[0].xyz) > 0.1;
\tok = ok && distance(pixelpos.xyz, o) < 64.0;
\treturn ok ? ${MAGENTA} : ${GREEN};
}
`;

// [Doruk-Aksoy] On a HUD image, DnD's ManaBar.fp shape: Zandronum 3.3 hands Process the draw's colour, whose alpha the mod packs a state into, and draws the alpha it returns.
const HUD_OLD = `uniform float timer;
vec4 Process(vec4 color)
{
\tbool ok = abs(color.a - 0.75) < 0.02 && timer > 0.0;
\tok = ok && getTexel(gl_TexCoord[0].st).a > 0.5 && texture2D(tex, gl_TexCoord[0].st).a > 0.5;
\treturn ok ? ${MAGENTA} : ${GREEN};
}
`;

// [Doruk-Aksoy] GZDoom's ProcessTexel on a HUD image, which the draw's colour multiplies after.
const HUD_NEW = `vec4 ProcessTexel()
{
\treturn getTexel(vTexCoord.st).a > 0.5 ? ${MAGENTA} : ${GREEN};
}
`;

const GLDEFS = `HardwareShader Texture "STARTAN2" { Shader "shaders/names.fp" }
HardwareShader Texture "BRICK1" { Shader "shaders/clash_a.fp" }
HardwareShader Texture "COMPBLUE" { Shader "shaders/clash_b.fp" }
HardwareShader ZXSKIN { Shader "shaders/model.fp" }
HardwareShader ZXHUDO { Shader "shaders/hudold.fp" }
HardwareShader ZXHUDN { Shader "shaders/hudnew.fp" }
`;

// [Doruk-Aksoy] Both HUD images at alpha 0.75, left and right of the view, as an OPEN script's HudMessages that stay; MAP02 draws plain magenta in their places, opaque where the old shader keeps its own alpha.
const HUD_AT = { old: { x: 0.15, y: 0.75 }, new: { x: 0.85, y: 0.75 } };
function hudScript(shaded) {
  const strings = shaded ? ["ZXHUDO", "ZXHUDN", "A"] : ["ZXMAGH", "ZXMAGH", "A"];
  const code = [
    PCD.PUSHNUMBER, 0, PCD.SETFONT, ...hudMessage(2, { id: 1, hold: 0, alpha: shaded ? 0.75 : 1.0, ...HUD_AT.old }),
    PCD.PUSHNUMBER, 1, PCD.SETFONT, ...hudMessage(2, { id: 2, hold: 0, alpha: 0.75, ...HUD_AT.new }),
    PCD.TERMINATE,
  ];
  return acs0(code, strings);
}

const solid = (rgb, n = 16) => { const px = Buffer.alloc(n * n * 3); for (let i = 0; i < n * n; i++) px.set(rgb, i * 3); return encodePNG(n, n, px); };

// [Doruk-Aksoy] One 32-unit cube each, the shaded skin from graphics/ so GLDEFS can name it, as DnD's are.
const ACTORS = [{ name: "ZXShaded", num: 20110, skin: "ZXSKIN.png" }, { name: "ZXMagenta", num: 20111, skin: "ZXMAGS.png" }];

function decorate() {
  return ACTORS.map((a) => `Actor ${a.name} ${a.num}\n{\n  +NOGRAVITY\n  +NOINTERACTION\n  States\n  {\n  Spawn:\n    ELEC A -1\n    Stop\n  }\n}\n`).join("\n");
}

function modeldef() {
  return ACTORS.map((a) => `Model ${a.name}\n{\n  Path "models/"\n  Model 0 "cube.kvx"\n  Path "graphics/"\n  Skin 0 "${a.skin}"\n` +
    "  Scale 4.0 4.0 4.0\n  FrameIndex ELEC A 0 0\n}\n").join("\n");
}

// [Doruk-Aksoy] MAP01's walls and cube carry the shaders; MAP02 has plain magenta in their places, which is what each shader draws when it finds what it reads.
function room(shaded) {
  const m = new UdmfMap();
  const wall = { north: "STARTAN2", south: "BRICK1", west: "COMPBLUE", east: "STARTAN3" };
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 },
    (edge) => ({ side: { texturemiddle: shaded || edge === "east" ? wall[edge] : "ZXMAG" } }));
  m.thing({ x: 400, y: 256, angle: 0, type: ACTORS[shaded ? 0 : 1].num });
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  return m;
}

export function buildHwShaderNamesPk3() {
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", room(true), { behavior: hudScript(true) })) },
    { name: "maps/MAP02.wad", data: wad(mapLumps("MAP02", room(false), { behavior: hudScript(false) })) },
    { name: "gldefs.txt", data: GLDEFS },
    { name: "shaders/names.fp", data: NAMES },
    { name: "shaders/clash_a.fp", data: clash("0.25", "<") },
    { name: "shaders/clash_b.fp", data: clash("0.75", ">") },
    { name: "shaders/model.fp", data: MODEL },
    { name: "shaders/hudold.fp", data: HUD_OLD },
    { name: "shaders/hudnew.fp", data: HUD_NEW },
    { name: "graphics/ZXHUDO.png", data: solid([128, 128, 128], 64) },
    { name: "graphics/ZXHUDN.png", data: solid([128, 128, 128], 64) },
    { name: "graphics/ZXMAGH.png", data: solid([255, 0, 255], 64) },
    { name: "decorate.txt", data: decorate() },
    { name: "modeldef.txt", data: modeldef() },
    { name: "models/cube.kvx", data: kvxBox(8, 8, 8) },
    { name: "graphics/ZXSKIN.png", data: solid([128, 128, 128]) },
    { name: "graphics/ZXMAGS.png", data: solid([255, 0, 255]) },
    { name: "textures/ZXMAG.png", data: solid([255, 0, 255]) },
  ]);
}

export const VANTAGES = [
  { name: "north", at: { x: 256, y: 256, z: 0, angle: 90, pitch: 0 } },
  { name: "south", at: { x: 256, y: 256, z: 0, angle: 270, pitch: 0 } },
  { name: "west", at: { x: 256, y: 256, z: 0, angle: 180, pitch: 0 } },
  { name: "east", at: { x: 256, y: 256, z: 0, angle: 0, pitch: 0 } },
];

// [Doruk-Aksoy] Each wall straight ahead above the weapon, and the cube on the floor ahead, each renderer against its own MAP02.
const AREAS = { names: ["north", [0.3, 0.2, 0.7, 0.45]], clashA: ["south", [0.3, 0.2, 0.7, 0.45]], clashB: ["west", [0.3, 0.2, 0.7, 0.45]],
  model: ["east", [0.47, 0.6, 0.53, 0.72]], hudOld: ["north", [0.135, 0.73, 0.165, 0.77]], hudNew: ["north", [0.835, 0.73, 0.865, 0.77]] };
export const BOXES = { MAP01: Object.fromEntries(["gl", "vk"].flatMap((r) => Object.entries(AREAS).map(([n, [vantage, box]]) =>
  [`${r}${n[0].toUpperCase()}${n.slice(1)}`, { vantage, box, pair: [r, `${r}Oracle`], ceiling: 3 }]))) };

export const SIDES = [{ name: "gl", gl: true, launchCvars: MUTE }, { name: "glOracle", gl: true, launchCvars: MUTE, commands: ["map MAP02"] },
  { name: "vk", launchCvars: MUTE }, { name: "vkOracle", launchCvars: MUTE, commands: ["map MAP02"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "hwshadernames", wad: buildHwShaderNamesPk3(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
