// [Doruk-Aksoy] UZDoom's GLDEFS shader rows on Vulkan, each wall against an oracle map in a dark room, with the SPIR-V optimizer off and on: exit 0 when Texture lines, Define, GetTexCoord, both ProcessLights, SetupMaterial and an old ProcessMaterial draw as UZDoom's main.fp would.
//
// usage:  node scenarios/hwshaderparity.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { encodePNG } from "../src/pngcore.mjs";
import { runScenarioIfMain, MUTE } from "../src/template.mjs";

const RED = [220, 40, 40], GREEN = [40, 200, 60], BLUE = [40, 60, 220], GREY = [160, 160, 160], YELLOWISH = [220, 200, 0];

// [Doruk-Aksoy] Each shader, a feature apiece; the walls they sit on are grey unless the feature reads them.
const SHADERS = {
  // [Doruk-Aksoy] Two Texture lines: red from one, green from the other.
  textures: "vec4 ProcessTexel()\n{\n\treturn vec4(texture(redtex, vTexCoord.st).r, texture(greentex, vTexCoord.st).g, 0.0, 1.0);\n}\n",
  // [Doruk-Aksoy] A Define picks the colour, another gives its blue.
  define: "vec4 ProcessTexel()\n{\n#ifdef ZXUSEBLUE\n\treturn vec4(40.0 / 255.0, 60.0 / 255.0, ZXBLUE, 1.0);\n#else\n\treturn vec4(1.0, 0.0, 1.0, 1.0);\n#endif\n}\n",
  // [Doruk-Aksoy] Every fragment reads the left half of a red-and-green texture, whose brightmap lights that half.
  texcoord: "vec2 GetTexCoord()\n{\n\treturn vec2(0.25, 0.5);\n}\n",
  // [Doruk-Aksoy] UZDoom's old ProcessLight(color), forwarded: full light whatever the room.
  oldlight: "vec4 ProcessTexel()\n{\n\treturn getTexel(vTexCoord.st);\n}\nvec4 ProcessLight(vec4 color)\n{\n\treturn vec4(1.0);\n}\n",
  // [Doruk-Aksoy] SetupMaterial's own colour and a white bright layer: blue at full light.
  setup: "void SetupMaterial(inout Material material)\n{\n\tmaterial.Base = vec4(40.0 / 255.0, 60.0 / 255.0, 220.0 / 255.0, 1.0);\n\tmaterial.Bright = vec4(1.0);\n}\n",
  // [Doruk-Aksoy] An old ProcessMaterial's own blue, its bright layer not added (LEGACY_USER_SHADER), so it is lit by the room.
  legacy: "Material ProcessMaterial()\n{\n\tMaterial m;\n\tm.Base = vec4(40.0 / 255.0, 60.0 / 255.0, 220.0 / 255.0, 1.0);\n\tm.Bright = vec4(1.0);\n\tm.Glow = vec4(0.0);\n\tm.Normal = vec3(0.0);\n\tm.Specular = vec3(0.0);\n\tm.Glossiness = 0.0;\n\tm.SpecularLevel = 0.0;\n\tm.Metallic = 0.0;\n\tm.Roughness = 0.0;\n\tm.AO = 0.0;\n\treturn m;\n}\n",
  // [Doruk-Aksoy] ProcessLight taking the material: its Base colour as the light, white here, so full light.
  newlight: "void SetupMaterial(inout Material material)\n{\n\tSetMaterialProps(material, vTexCoord.st);\n}\nvec4 ProcessLight(Material material, vec4 color)\n{\n\treturn vec4(1.0);\n}\n",
  // [Doruk-Aksoy] Zandronum 3.3's Process(color) handed the light ProcessLight made full.
  oldprocess: "vec4 Process(vec4 color)\n{\n\treturn getTexel(gl_TexCoord[0].st) * color;\n}\nvec4 ProcessLight(vec4 color)\n{\n\treturn vec4(1.0);\n}\n",
  // [Doruk-Aksoy] A sprite reading its Texture line, through the frame's dynamic reservation.
  sprite: "vec4 ProcessTexel()\n{\n\treturn vec4(texture(bluetex, vTexCoord.st).rgb, getTexel(vTexCoord.st).a);\n}\n",
};

const GLDEFS = `HardwareShader Texture "ZXPTA" { Shader "shaders/textures.fp" Texture redtex "ZXRED" Texture greentex "ZXGRN" }
HardwareShader Texture "ZXPTB" { Shader "shaders/define.fp" Define ZXUSEBLUE Define ZXBLUE = "(220.0 / 255.0)" }
HardwareShader Texture "ZXPTC" { Shader "shaders/texcoord.fp" }
HardwareShader Texture "ZXPTD" { Shader "shaders/oldlight.fp" }
HardwareShader Texture "ZXPTE" { Shader "shaders/setup.fp" }
HardwareShader Texture "ZXPTF" { Shader "shaders/legacy.fp" }
HardwareShader Texture "ZXPTG" { Shader "shaders/newlight.fp" }
HardwareShader Texture "ZXPTH" { Shader "shaders/oldprocess.fp" }
HardwareShader Sprite "ZXPSA0" { Shader "shaders/sprite.fp" Texture bluetex "ZXBLU" }
brightmap texture ZXPTC { map ZXHALF }
brightmap texture ZXPTA { map ZXWHITE }
brightmap texture ZXPTB { map ZXWHITE }
brightmap texture ZXORYEL { map ZXWHITE }
brightmap texture ZXORBLW { map ZXWHITE }
brightmap texture ZXORLIT { map ZXWHITE }
brightmap texture ZXORBLU { map ZXWHITE }
brightmap texture ZXORRED { map ZXWHITE }
`;

// [Doruk-Aksoy] A square of one colour, a sprite's grabbed at its bottom middle so it stands on its actor.
export const solid = (rgb, n = 64, grab = null) => { const px = Buffer.alloc(n * n * 3); for (let i = 0; i < n * n; i++) px.set(rgb, i * 3); return encodePNG(n, n, px, { grab }); };
// [Doruk-Aksoy] Left half `left`, right half `right`.
const halves = (left, right, n = 64) => { const px = Buffer.alloc(n * n * 3); for (let i = 0; i < n * n; i++) px.set((i % n) < n / 2 ? left : right, i * 3); return encodePNG(n, n, px); };

// [Doruk-Aksoy] Each map pair's walls, shaded and oracle: what each feature should draw, in the room's light or fully lit under a white brightmap (the Texture and Define walls too, so their colours stand apart).
const PAIRS = [
  { north: ["ZXPTA", "ZXORYEL"], south: ["ZXPTB", "ZXORBLW"], west: ["ZXPTC", "ZXORRED"], east: ["ZXPTD", "ZXORLIT"] },
  { north: ["ZXPTE", "ZXORBLU"], south: ["ZXPTF", "ZXORBLD"], west: ["ZXPTG", "ZXORLIT"], east: ["ZXPTH", "ZXORLIT"] },
];

const ACTORS = [{ name: "ZXParitySprite", num: 20130, sprite: "ZXPS" }, { name: "ZXParityOracle", num: 20131, sprite: "ZXPO" }];

// [Doruk-Aksoy] Each wall and the angle that faces it from the room's middle.
const VANTAGE_ANGLES = [["north", 90], ["south", 270], ["west", 180], ["east", 0]];

function decorate() {
  return ACTORS.map((a) => `Actor ${a.name} ${a.num}\n{\n  +NOGRAVITY\n  +NOINTERACTION\n  States\n  {\n  Spawn:\n    ${a.sprite} A -1\n    Stop\n  }\n}\n`).join("\n");
}

// [Doruk-Aksoy] A room on pair `pair`'s walls, shaded or the oracle's, with the sprite in the first pair and the player facing `angle`; the second is lit at 160, where an old ProcessMaterial's room-lit blue stands apart from grey and from full light.
function room(pair, shaded, angle = 90) {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: pair === 1 ? 160 : 64 },
    (edge) => ({ side: { texturemiddle: PAIRS[pair][edge][shaded ? 0 : 1] } }));
  if (pair === 0) m.thing({ x: 256, y: 380, angle: 270, type: ACTORS[shaded ? 0 : 1].num });
  m.thing({ x: 256, y: 256, angle, type: 1 });
  return m;
}

const mapName = (n) => `MAP${String(n).padStart(2, "0")}`;

// [Doruk-Aksoy] The maps: each pair shaded then its oracle (MAP01 to MAP04), or with `perWall` one map per pair, shading and wall, the player facing it, for a port with no console to turn him (MAP01 to MAP16 in PER_WALL's order).
export const PER_WALL = [0, 1].flatMap((pair) => [true, false].flatMap((shaded) => VANTAGE_ANGLES.map(([wall, angle]) => ({ pair, shaded, wall, angle }))));
function parityMaps(perWall) {
  if (perWall) return PER_WALL.map((w, i) => [mapName(i + 1), room(w.pair, w.shaded, w.angle)]);
  return [[1, 0, true], [2, 0, false], [3, 1, true], [4, 1, false]].map(([n, pair, shaded]) => [mapName(n), room(pair, shaded)]);
}

export function buildHwShaderParityPk3({ perWall = false } = {}) {
  return pk3([
    ...parityMaps(perWall).map(([name, m]) => ({ name: `maps/${name}.wad`, data: wad(mapLumps(name, m)) })),
    { name: "gldefs.txt", data: GLDEFS },
    ...Object.entries(SHADERS).map(([name, src]) => ({ name: `shaders/${name}.fp`, data: src })),
    ...["ZXPTA", "ZXPTB", "ZXPTD", "ZXPTE", "ZXPTF", "ZXPTG", "ZXPTH", "ZXORLIT"].map((t) => ({ name: `textures/${t}.png`, data: solid(GREY) })),
    { name: "textures/ZXPTC.png", data: halves(RED, GREEN) },
    { name: "textures/ZXHALF.png", data: halves([255, 255, 255], [0, 0, 0]) },
    { name: "textures/ZXRED.png", data: solid([255, 0, 0]) },
    { name: "textures/ZXGRN.png", data: solid([0, 255, 0]) },
    { name: "textures/ZXBLU.png", data: solid(BLUE) },
    { name: "textures/ZXORYEL.png", data: solid([255, 255, 0]) },
    { name: "textures/ZXORBLD.png", data: solid(BLUE) },
    { name: "textures/ZXORBLW.png", data: solid(BLUE) },
    { name: "textures/ZXORBLU.png", data: solid(BLUE) },
    { name: "textures/ZXORRED.png", data: solid(RED) },
    { name: "textures/ZXWHITE.png", data: solid([255, 255, 255], 8) },
    { name: "sprites/ZXPSA0.png", data: solid(GREY, 64, [32, 64]) },
    { name: "sprites/ZXPOA0.png", data: solid(BLUE, 64, [32, 64]) },
    { name: "decorate.txt", data: decorate() },
  ]);
}

export const VANTAGES = VANTAGE_ANGLES.map(([name, angle]) => ({ name, at: { x: 256, y: 256, z: 0, angle, pitch: 0 } }));

// [Doruk-Aksoy] Each wall above the weapon (the north one above the sprite), per map pair, and the first pair's sprite.
const AREAS = [
  { textures: ["north", [0.3, 0.22, 0.7, 0.32]], define: ["south", [0.3, 0.2, 0.7, 0.45]], texcoord: ["west", [0.3, 0.2, 0.7, 0.45]],
    oldLight: ["east", [0.3, 0.2, 0.7, 0.45]], sprite: ["north", [0.45, 0.45, 0.55, 0.6]] },
  { setup: ["north", [0.3, 0.2, 0.7, 0.45]], legacy: ["south", [0.3, 0.2, 0.7, 0.45]], newLight: ["west", [0.3, 0.2, 0.7, 0.45]],
    oldProcess: ["east", [0.3, 0.2, 0.7, 0.45]] },
];
const RUNS = ["vk", "vkOpt"];
const cap = (s) => s[0].toUpperCase() + s.slice(1);
// [Doruk-Aksoy] All under MAP01, the map every side launches on before its own command loads its pair's.
export const BOXES = { MAP01: Object.fromEntries([0, 1].flatMap((pair) => RUNS.flatMap((r) =>
  Object.entries(AREAS[pair]).map(([n, [vantage, box]]) => [`${r}${cap(n)}`, { vantage, box, pair: [`${r}${pair}`, `${r}${pair}Oracle`], ceiling: 3 }])))) };

// [Doruk-Aksoy] Each run and its oracle per map pair, the optimizer off and on.
const OPT = { ...MUTE, fua_dg_spirv_optimize: 1 };
const NOOPT = { ...MUTE, fua_dg_spirv_optimize: 0 };
export const SIDES = [0, 1].flatMap((pair) => [
  { name: `vk${pair}`, launchCvars: NOOPT, commands: [`map MAP0${pair * 2 + 1}`] }, { name: `vk${pair}Oracle`, launchCvars: NOOPT, commands: [`map MAP0${pair * 2 + 2}`] },
  { name: `vkOpt${pair}`, launchCvars: OPT, commands: [`map MAP0${pair * 2 + 1}`] }, { name: `vkOpt${pair}Oracle`, launchCvars: OPT, commands: [`map MAP0${pair * 2 + 2}`] },
]);

runScenarioIfMain(import.meta.url, () => ({ name: "hwshaderparity", wad: buildHwShaderParityPk3(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
