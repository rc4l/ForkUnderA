// [Doruk-Aksoy] GLDEFS post-process shaders on Vulkan, captured after each is turned on from the console: exit 0 when one that copies its input changes nothing at full and half scale, GL's y-up TexCoord and gl_FragCoord read the right way round, two run in order, a scene shader runs over the weapon but not the HUD, a beforebloom one under the weapon, a screen one over the HUD, a uniform set from the console takes, and turning them off leaves the frame as it was.
//
// usage:  node scenarios/postprocess.mjs [port]
import { UdmfMap, wad, pk3, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareSequence } from "../src/template.mjs";

const block = (stage, name, lump, extra = "") => `HardwareShader PostProcess ${stage}\n{\n\tName "${name}"\n\tShader "shaders/pp/${lump}.fp" 330\n${extra}}\n`;
const GREEN = "\tUniform vec3 fillColor = 0.0 1.0 0.0\n";

export const GLDEFS = [
  block("scene", "identity", "identity"),
  block("scene", "fragcoord", "fragcoord"),
  block("scene", "bottomred", "bottomred"),
  block("scene", "invert1", "invert"),
  block("scene", "invert2", "invert"),
  block("scene", "green", "fill", GREEN),
  block("beforebloom", "greenbefore", "fill", GREEN),
  block("screen", "greenscreen", "fill", GREEN),
  block("scene", "lastframe", "lastframe"),
  block("scene", "textured", "textured", "\tTexture pic \"STARTAN2\"\n"),
  block("scene", "missing", "textured", "\tTexture pic \"NOSUCHTX\"\n"),
  block("scene", "white", "fill", "\tUniform vec3 fillColor = 1.0 1.0 1.0\n"),
  // [Doruk-Aksoy] With the SPIR-V optimizer on, the first is left with no resources at all and the third without its unused uniform and texture, which a layout naming them hung on.
  block("scene", "constant", "constant"),
  block("scene", "constfill", "fill", "\tUniform vec3 fillColor = 0.25 0.5 0.75\n"),
  block("scene", "unused", "identity", "\tUniform float unusedValue = 2.0\n\tTexture unusedPic \"STARTAN2\"\n"),
].join("");

export const SHADERS = {
  identity: "void main()\n{\n\tFragColor = texture(InputTexture, TexCoord);\n}\n",
  // [Doruk-Aksoy] The same copy through gl_FragCoord and textureSize, which must agree with TexCoord at any scale.
  fragcoord: "void main()\n{\n\tFragColor = texture(InputTexture, gl_FragCoord.xy / vec2(textureSize(InputTexture, 0)));\n}\n",
  // [Doruk-Aksoy] GL's TexCoord has y up, so its lower half is the screen's bottom.
  bottomred: "void main()\n{\n\tFragColor = TexCoord.y < 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : texture(InputTexture, TexCoord);\n}\n",
  invert: "void main()\n{\n\tvec4 c = texture(InputTexture, TexCoord);\n\tFragColor = vec4(1.0 - c.rgb, 1.0);\n}\n",
  fill: "void main()\n{\n\tFragColor = vec4(fillColor, 1.0);\n}\n",
  // [Doruk-Aksoy] The previous frame, which a still view makes the same as this one.
  lastframe: "void main()\n{\n\tFragColor = texture(LastInputTexture, TexCoord);\n}\n",
  textured: "void main()\n{\n\tFragColor = vec4(texture(pic, TexCoord).rgb, 1.0);\n}\n",
  constant: "void main()\n{\n\tFragColor = vec4(0.25, 0.5, 0.75, 1.0);\n}\n",
};

// [Doruk-Aksoy] One lit hall with detailed walls, so a picture moved, flipped or scaled shows.
export function buildPostProcessPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 208 },
    (edge) => ({ side: { texturemiddle: edge === "north" ? "STARTAN2" : "BROWN1" } }));
  m.thing({ x: 512, y: 300, angle: 90, type: 1 });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) },
    { name: "gldefs.txt", data: GLDEFS },
    ...Object.entries(SHADERS).map(([lump, text]) => ({ name: `shaders/pp/${lump}.fp`, data: text })),
  ]);
}

export const VANTAGES = [{ name: "hall", at: { x: 512, y: 300, z: 0, angle: 90, pitch: 0 } }];

const on = (name) => `shaderenable ${name} 1`;
const off = (name) => `shaderenable ${name} 0`;
// [Doruk-Aksoy] The weapon shown throughout, and the HUD hidden (screenblocks 12) only to show what draws over it; midway the SPIR-V optimizer flips to `other` and back to `home`, which makes every program again.
export const buildSteps = (other, home) => [
  { name: "plain", commands: ["crosshair 0", "r_drawplayersprites 1", 1500] },
  { name: "identity", commands: [on("identity"), 800] },
  { name: "fragcoord", commands: [off("identity"), on("fragcoord"), 800] },
  { name: "bottomred", commands: [off("fragcoord"), on("bottomred"), 800] },
  { name: "inverted", commands: [off("bottomred"), on("invert1"), 800] },
  { name: "twice", commands: [on("invert2"), 800] },
  { name: "green", commands: [off("invert1"), off("invert2"), on("green"), 800] },
  { name: "greenNoWeapon", commands: ["r_drawplayersprites 0", 800] },
  { name: "greenNoHud", commands: ["r_drawplayersprites 1", "screenblocks 12", 800] },
  { name: "red", commands: ["screenblocks 11", "shaderuniform green fillColor 1 0 0", 800] },
  { name: "before", commands: [off("green"), on("greenbefore"), 800] },
  { name: "beforeNoWeapon", commands: ["r_drawplayersprites 0", 800] },
  { name: "screen", commands: [off("greenbefore"), "r_drawplayersprites 1", on("greenscreen"), 800] },
  { name: "screenNoHud", commands: ["screenblocks 12", 800] },
  { name: "plainAgain", commands: [off("greenscreen"), "screenblocks 11", 800] },
  { name: "lastframe", commands: [on("lastframe"), 800] },
  { name: "textured", commands: [off("lastframe"), on("textured"), 800] },
  { name: "missing", commands: [off("textured"), on("missing"), 800] },
  { name: "white", commands: [off("missing"), on("white"), 800] },
  { name: "plainThird", commands: [off("white"), 800] },
  { name: "constant", commands: [on("constant"), 800] },
  { name: "constFill", commands: [off("constant"), on("constfill"), 800] },
  { name: "unused", commands: [off("constfill"), on("unused"), 800] },
  { name: "flipPlain", commands: [off("unused"), `fua_dg_spirv_optimize ${other}`, 3000] },
  { name: "flipIdentity", commands: [on("identity"), 1000] },
  { name: "flipConstant", commands: [off("identity"), on("constant"), 800] },
  { name: "flipConstFill", commands: [off("constant"), on("constfill"), 800] },
  { name: "flipUnused", commands: [off("constfill"), on("unused"), 800] },
  { name: "flipWhite", commands: [off("unused"), on("white"), 800] },
  { name: "backIdentity", commands: [off("white"), `fua_dg_spirv_optimize ${home}`, 3000, on("identity"), 1000] },
  // [Doruk-Aksoy] Last, since it stays at half scale: the scene shaders read the share of their picture the scene covers.
  { name: "halfPlain", commands: [off("identity"), "fua_dg_dynres_force 0.5", 1500] },
  { name: "halfIdentity", commands: [on("identity"), 800] },
  { name: "halfFragcoord", commands: [off("identity"), on("fragcoord"), 800] },
  { name: "halfBottomred", commands: [off("fragcoord"), on("bottomred"), 800] },
  { name: "halfLastframe", commands: [off("bottomred"), on("lastframe"), 800] },
];

// [Doruk-Aksoy] Below the console's lines at the top, which each step's command prints anew, and above the HUD's numbers.
const VIEW = [0.0, 0.06, 1.0, 0.95];
const TOP = [0.0, 0.06, 1.0, 0.42];
const BOTTOM = [0.0, 0.56, 1.0, 0.8];
const WEAPON = [0.44, 0.65, 0.57, 0.95];
const HUD = [0.0, 0.955, 0.09, 0.995];
export const BOXES = { MAP01: {
  identity: { box: VIEW, pair: ["plain", "identity"], ceiling: 0.5 },
  fragcoord: { box: VIEW, pair: ["plain", "fragcoord"], ceiling: 0.5 },
  upIsUp: { box: TOP, pair: ["plain", "bottomred"], ceiling: 0.5 },
  bottomIsRed: { box: BOTTOM, pair: ["plain", "bottomred"], floor: 20 },
  inverted: { box: VIEW, pair: ["plain", "inverted"], floor: 20 },
  twiceInOrder: { box: VIEW, pair: ["plain", "twice"], ceiling: 0.5 },
  weaponUnderScene: { box: WEAPON, pair: ["green", "greenNoWeapon"], ceiling: 0.5 },
  hudOverScene: { box: HUD, pair: ["green", "greenNoHud"], floor: 10 },
  consoleUniform: { box: VIEW, pair: ["green", "red"], floor: 50 },
  weaponOverBeforeBloom: { box: WEAPON, pair: ["before", "beforeNoWeapon"], floor: 5 },
  screenOverHud: { box: HUD, pair: ["screen", "screenNoHud"], ceiling: 0.5 },
  offIsOff: { box: [0.0, 0.06, 1.0, 1.0], pair: ["plain", "plainAgain"], ceiling: 0.5 },
  lastFrame: { box: VIEW, pair: ["plain", "lastframe"], ceiling: 0.5 },
  texture: { box: VIEW, pair: ["textured", "white"], floor: 30 },
  missingIsWhite: { box: VIEW, pair: ["missing", "white"], ceiling: 0.5 },
  offAgain: { box: [0.0, 0.06, 1.0, 1.0], pair: ["plain", "plainThird"], ceiling: 0.5 },
  constantIsConstFill: { box: VIEW, pair: ["constant", "constFill"], ceiling: 0.5 },
  unusedIsIdentity: { box: VIEW, pair: ["plain", "unused"], ceiling: 0.5 },
  flipKeepsTheWorld: { box: VIEW, pair: ["plain", "flipPlain"], ceiling: 1 },
  flipIdentity: { box: VIEW, pair: ["flipPlain", "flipIdentity"], ceiling: 0.5 },
  flipConstant: { box: VIEW, pair: ["constant", "flipConstant"], ceiling: 0.5 },
  flipConstFill: { box: VIEW, pair: ["constFill", "flipConstFill"], ceiling: 0.5 },
  flipUnused: { box: VIEW, pair: ["flipPlain", "flipUnused"], ceiling: 0.5 },
  flipWhite: { box: VIEW, pair: ["white", "flipWhite"], ceiling: 0.5 },
  backIdentity: { box: VIEW, pair: ["plain", "backIdentity"], ceiling: 1 },
  halfIdentity: { box: VIEW, pair: ["halfPlain", "halfIdentity"], ceiling: 1 },
  halfFragcoord: { box: VIEW, pair: ["halfPlain", "halfFragcoord"], ceiling: 1 },
  halfUpIsUp: { box: TOP, pair: ["halfPlain", "halfBottomred"], ceiling: 1 },
  halfBottomIsRed: { box: BOTTOM, pair: ["halfPlain", "halfBottomred"], floor: 20 },
  halfLastFrame: { box: VIEW, pair: ["halfPlain", "halfLastframe"], ceiling: 1 },
} };

// [Doruk-Aksoy] Launched with the optimizer as desktop Vulkan has it (off), flipping it on midway; postprocessopt.mjs launches it on.
export const STEPS = buildSteps(1, -1);

runScenarioIfMain(import.meta.url, () => ({ name: "postprocess", wad: buildPostProcessPk3(), maps: ["MAP01"],
  vantages: VANTAGES, steps: STEPS, boxes: BOXES }), compareSequence);
