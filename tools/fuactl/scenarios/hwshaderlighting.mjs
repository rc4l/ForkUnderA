// [Doruk-Aksoy] Old Process(color) shaders lit by their contract in a dark room, plain and in coloured fog, each renderer against an oracle map, and Vulkan again with the SPIR-V optimizer: exit 0 when a Zandronum 3.3 shader's unlit glow stays bright, its lit texel darkens with the room, and a GLDEFS fua_ProcessLighting uzdoom one or a ProcessTexel one is lit after as UZDoom lights it.
//
// usage:  node scenarios/hwshaderlighting.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { encodePNG } from "../src/pngcore.mjs";
import { runScenarioIfMain, MUTE } from "../src/template.mjs";

const ORANGE = [255, 128, 0], GREY = [160, 160, 160];
const GLOW = "vec4(1.0, 128.0 / 255.0, 0.0, 1.0)";

// [Doruk-Aksoy] DnD's Volcano and DoomguardLava shape: a glow returned without the light it is handed.
const OLD_GLOW = `vec4 Process(vec4 color)\n{\n\treturn ${GLOW};\n}\n`;
// [Doruk-Aksoy] A texel lit by the light 3.3 hands Process, as DnD lights its stone and armour.
const OLD_LIT = "vec4 Process(vec4 color)\n{\n\treturn getTexel(gl_TexCoord[0].st) * color;\n}\n";
const NEW_GLOW = `vec4 ProcessTexel()\n{\n\treturn ${GLOW};\n}\n`;

const GLDEFS = `HardwareShader Texture "ZXSHA" { Shader "shaders/oldglow.fp" }
HardwareShader Texture "ZXSHB" { Shader "shaders/oldlit.fp" }
HardwareShader Texture "ZXSHC" { Shader "shaders/oldglow.fp" fua_ProcessLighting uzdoom }
HardwareShader Texture "ZXSHD" { Shader "shaders/newglow.fp" }
HardwareShader Sprite "ZXGLA0" { Shader "shaders/oldglow.fp" }
brightmap texture ZXORF { map ZXWHITE }
brightmap sprite ZXORA0 { map ZXWHITE }
`;

// [Doruk-Aksoy] A square of one colour; a sprite's is grabbed at its bottom middle so it stands on its actor.
const solid = (rgb, n = 64, grab = null) => { const px = Buffer.alloc(n * n * 3); for (let i = 0; i < n * n; i++) px.set(rgb, i * 3); return encodePNG(n, n, px, { grab }); };

// [Doruk-Aksoy] The shaded walls and what each should draw: an unlit glow is orange under a white brightmap, lit fully whatever the room, and anything lit is a plain texture in the room's light.
const WALLS = {
  north: { shaded: "ZXSHA", oracle: "ZXORF" },
  south: { shaded: "ZXSHB", oracle: "ZXGRY" },
  west: { shaded: "ZXSHC", oracle: "ZXORG" },
  east: { shaded: "ZXSHD", oracle: "ZXORG" },
};

// [Doruk-Aksoy] A sprite with the old glow, and its oracle the same square in orange under a white brightmap, fully lit and then fogged as the glow should be (a Bright frame is not, as Vulkan leaves it unfogged).
const ACTORS = [{ name: "ZXGlowSprite", num: 20120, sprite: "ZXGL" }, { name: "ZXOrangeSprite", num: 20121, sprite: "ZXOR" }];

function decorate() {
  return ACTORS.map((a) => `Actor ${a.name} ${a.num}\n{\n  +NOGRAVITY\n  +NOINTERACTION\n  States\n  {\n  Spawn:\n    ${a.sprite} A -1\n    Stop\n  }\n}\n`).join("\n");
}

// [Doruk-Aksoy] A dark room, with a blue coloured fog when `fog`.
function room(shaded, fog) {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 64,
    ...(fog ? { fadecolor: 0x203060 } : {}) },
  (edge) => ({ side: { texturemiddle: WALLS[edge][shaded ? "shaded" : "oracle"] } }));
  m.thing({ x: 256, y: 380, angle: 270, type: ACTORS[shaded ? 0 : 1].num });
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  return m;
}

export function buildHwShaderLightingPk3() {
  const maps = [[1, true, false], [2, false, false], [3, true, true], [4, false, true]];
  return pk3([
    ...maps.map(([n, shaded, fog]) => ({ name: `maps/MAP0${n}.wad`, data: wad(mapLumps(`MAP0${n}`, room(shaded, fog))) })),
    { name: "gldefs.txt", data: GLDEFS },
    { name: "shaders/oldglow.fp", data: OLD_GLOW },
    { name: "shaders/oldlit.fp", data: OLD_LIT },
    { name: "shaders/newglow.fp", data: NEW_GLOW },
    ...["ZXSHA", "ZXSHC", "ZXSHD", "ZXORG", "ZXORF"].map((t) => ({ name: `textures/${t}.png`, data: solid(ORANGE) })),
    ...["ZXSHB", "ZXGRY"].map((t) => ({ name: `textures/${t}.png`, data: solid(GREY) })),
    { name: "textures/ZXWHITE.png", data: solid([255, 255, 255], 8) },
    { name: "sprites/ZXGLA0.png", data: solid(GREY, 64, [32, 64]) },
    { name: "sprites/ZXORA0.png", data: solid(ORANGE, 64, [32, 64]) },
    { name: "decorate.txt", data: decorate() },
  ]);
}

export const VANTAGES = [
  { name: "north", at: { x: 256, y: 256, z: 0, angle: 90, pitch: 0 } },
  { name: "south", at: { x: 256, y: 256, z: 0, angle: 270, pitch: 0 } },
  { name: "west", at: { x: 256, y: 256, z: 0, angle: 180, pitch: 0 } },
  { name: "east", at: { x: 256, y: 256, z: 0, angle: 0, pitch: 0 } },
];

// [Doruk-Aksoy] Each wall above the weapon (the north one above the sprite), and the sprite ahead.
const AREAS = { oldGlow: ["north", [0.3, 0.22, 0.7, 0.32]], oldLit: ["south", [0.3, 0.2, 0.7, 0.45]], uzdoomGlow: ["west", [0.3, 0.2, 0.7, 0.45]],
  newGlow: ["east", [0.3, 0.2, 0.7, 0.45]], sprite: ["north", [0.45, 0.45, 0.55, 0.6]] };
const RUNS = ["gl", "glFog", "vk", "vkFog", "vkOptFog"];
export const BOXES = { MAP01: Object.fromEntries(RUNS.flatMap((r) => Object.entries(AREAS).map(([n, [vantage, box]]) =>
  [`${r}${n[0].toUpperCase()}${n.slice(1)}`, { vantage, box, pair: [r, `${r}Oracle`], ceiling: 3 }]))) };

// [Doruk-Aksoy] Each run and its oracle: the shaded map against the plain one, plain or fogged, Vulkan with the optimizer on too.
const OPT = { ...MUTE, fua_dg_spirv_optimize: 1 };
export const SIDES = [
  { name: "gl", gl: true, launchCvars: MUTE }, { name: "glOracle", gl: true, launchCvars: MUTE, commands: ["map MAP02"] },
  { name: "glFog", gl: true, launchCvars: MUTE, commands: ["map MAP03"] }, { name: "glFogOracle", gl: true, launchCvars: MUTE, commands: ["map MAP04"] },
  { name: "vk", launchCvars: MUTE }, { name: "vkOracle", launchCvars: MUTE, commands: ["map MAP02"] },
  { name: "vkFog", launchCvars: MUTE, commands: ["map MAP03"] }, { name: "vkFogOracle", launchCvars: MUTE, commands: ["map MAP04"] },
  { name: "vkOptFog", launchCvars: OPT, commands: ["map MAP03"] }, { name: "vkOptFogOracle", launchCvars: OPT, commands: ["map MAP04"] },
];

runScenarioIfMain(import.meta.url, () => ({ name: "hwshaderlighting", wad: buildHwShaderLightingPk3(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
