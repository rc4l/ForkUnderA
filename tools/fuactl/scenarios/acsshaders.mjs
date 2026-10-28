// [Doruk-Aksoy] The fua_ ACS shader functions on Vulkan, single-player, each run by a puked script: exit 0 when every SetShaderUniform kind lands in its uniform behind another, a player's own value wins over everyone's, a timed enable goes off when its tics run out and later than that when enabled again meanwhile, a savegame brings the state back, a new level starts from GLDEFS, and a LOADACS library's calls return what they should with undeclared names ignored.
//
// usage:  node scenarios/acsshaders.mjs [port]
import { UdmfMap, wad, pk3, mapLumps } from "../src/udmf.mjs";
import { acsScripts, ACSF, AcsScript, PCD } from "../src/acs.mjs";
import { runScenarioIfMain, compareSequence } from "../src/template.mjs";

const block = (name, lump, uniforms = "") => `HardwareShader PostProcess scene\n{\n\tName "${name}"\n\tShader "shaders/acs/${lump}.fp" 330\n${uniforms}}\n`;
// [Doruk-Aksoy] Each uniform the scripts set sits behind a float, so it lands at a std140 offset other than 0.
const behind = (decl) => `\tUniform float pad = 0.0\n\tUniform ${decl}\n`;

export const GLDEFS = [
  block("u1f", "u1f", behind("float a = 0.0")),
  block("u2f", "u2f", behind("vec2 b = 0.0 0.0")),
  block("u3f", "u3f", behind("vec3 c = 0.0 0.0 0.0")),
  block("u4f", "u4f", behind("vec4 d = 0.0 0.0 0.0 0.0")),
  block("u1i", "u1i", behind("int m = 0")),
  ...["red", "yellow", "blue", "white", "green"].map((c) => block(c, c)),
  block("timed", "red"),
  // [Doruk-Aksoy] u4f's twin, for a vec4 netshaders.mjs sends that no player's own value hides.
  block("u4net", "u4f", behind("vec4 d = 0.0 0.0 0.0 0.0")),
].join("");

const fill = (rgba) => `void main()\n{\n\tFragColor = ${rgba};\n}\n`;
export const SHADERS = {
  u1f: fill("vec4(a, 0.0, 0.0, 1.0)"),
  u2f: fill("vec4(b, 0.0, 1.0)"),
  u3f: fill("vec4(c, 1.0)"),
  u4f: fill("vec4(d.xyz * d.w, 1.0)"),
  u1i: fill("m == 3 ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0)"),
  red: fill("vec4(1.0, 0.0, 0.0, 1.0)"),
  yellow: fill("vec4(1.0, 1.0, 0.0, 1.0)"),
  blue: fill("vec4(0.0, 0.0, 1.0, 1.0)"),
  white: fill("vec4(1.0)"),
  green: fill("vec4(0.0, 1.0, 0.0, 1.0)"),
};

const flag = (v) => (v === undefined ? [] : [v ? 1 : 0]);

// [Doruk-Aksoy] The words of the fua_ shader calls over a string table, each name pushed as its index, tagged as a library's strings are when `tagged`; the ...Result forms leave the call's result on the stack.
export function shaderCalls(strings, { tagged = false } = {}) {
  const str = (name) => {
    const i = strings.indexOf(name);
    if (i < 0) throw new Error(`no string ${name}`);
    return [PCD.PUSHNUMBER, i, ...(tagged ? [PCD.TAGSTRING] : [])];
  };
  const call = (fn, args) => [...args.flatMap((a) => (typeof a === "string" ? str(a) : [PCD.PUSHNUMBER, a])), PCD.CALLFUNC, args.length, fn];
  // [Doruk-Aksoy] fua_SetShaderEnabled(shader, on [, activatorOnly [, tics]]).
  const enableResult = (shader, on, activatorOnly, tics) =>
    call(ACSF.fua_SetShaderEnabled, [shader, on ? 1 : 0, ...flag(activatorOnly ?? (tics === undefined ? undefined : false)), ...(tics === undefined ? [] : [tics])]);
  // [Doruk-Aksoy] fua_SetShaderUniform1f..4f with as many values as given (in map units, sent as fixed point), or 1i when `int`.
  const uniformResult = (shader, name, values, { int = false, own } = {}) =>
    call(int ? ACSF.fua_SetShaderUniform1i : ACSF.fua_SetShaderUniform1f + values.length - 1,
      [shader, name, ...values.map((v) => (int ? v : Math.round(v * 65536))), ...flag(own)]);
  return {
    enableResult, uniformResult,
    enable: (...a) => [...enableResult(...a), PCD.DROP],
    uniform: (...a) => [...uniformResult(...a), PCD.DROP],
  };
}

// [Doruk-Aksoy] Every string the map's scripts pass, by name, as their string table holds them.
export const STRINGS = ["u1f", "a", "u2f", "b", "u3f", "c", "u4f", "d", "u1i", "m", "red", "timed", "u4net"];
export const { enable, uniform } = shaderCalls(STRINGS);

// [Doruk-Aksoy] The closed scripts the steps puke, by number.
export const SCRIPTS = {
  1: [...uniform("u1f", "a", [1]), ...enable("u1f", true)],
  2: [...enable("u1f", false), ...uniform("u2f", "b", [1, 1]), ...enable("u2f", true)],
  3: [...enable("u2f", false), ...uniform("u3f", "c", [0, 0, 1]), ...enable("u3f", true)],
  4: [...enable("u3f", false), ...uniform("u4f", "d", [1, 1, 1, 1]), ...enable("u4f", true)],
  5: [...uniform("u4f", "d", [1, 1, 1, 0])],
  6: [...enable("u4f", false), ...enable("u1i", true)],
  7: [...uniform("u1i", "m", [3], { int: true })],
  // [Doruk-Aksoy] Everyone's c is still blue; the activator's own yellow wins, puked by the console player.
  8: [...enable("u1i", false), ...enable("u3f", true), ...uniform("u3f", "c", [1, 1, 0], { own: true })],
  9: [...enable("u3f", false), ...enable("red", true, false, 105)],
  10: [...enable("u3f", true)],
  // [Doruk-Aksoy] Three seconds of red, puked twice two seconds apart, which restarts the three.
  11: [...enable("red", true, false, 105)],
};

// [Doruk-Aksoy] The library LOADACS loads, its strings tagged as a library's are, its own table.
export const LIBRARY = "FUASHLIB";
export const LIBRARY_STRINGS = ["nosuch", "u3f", "c", "green", "red"];

// [Doruk-Aksoy] Library script 21: green when every call returns what it should (0 for an undeclared shader or uniform, 1 for a declared one, set again or not), else red.
export function libraryScript() {
  const lib = shaderCalls(LIBRARY_STRINGS, { tagged: true });
  const s = new AcsScript();
  const expect = (words, want) => s.emit(...words, PCD.PUSHNUMBER, want, PCD.EQ).jump(PCD.IFNOTGOTO, "fail");
  expect(lib.enableResult("nosuch", true), 0);
  expect(lib.uniformResult("u3f", "nosuch", [1]), 0);
  expect(lib.uniformResult("nosuch", "c", [1, 1, 1]), 0);
  expect(lib.uniformResult("u3f", "c", [0, 1, 0]), 1);
  expect(lib.enableResult("u3f", false), 1);
  expect(lib.enableResult("u3f", false), 1);
  s.emit(...lib.enable("green", true), PCD.TERMINATE);
  s.label("fail").emit(...lib.enable("red", true), PCD.TERMINATE);
  return acsScripts([{ open: false, number: 21, code: s.words() }], LIBRARY_STRINGS, { enhanced: true });
}

// [Doruk-Aksoy] One lit hall whose BEHAVIOR holds SCRIPTS, with the library, LOADACS and the shaders beside it in a pk3.
export function buildAcsShadersPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 208 },
    (edge) => ({ side: { texturemiddle: edge === "north" ? "STARTAN2" : "BROWN1" } }));
  m.thing({ x: 512, y: 300, angle: 90, type: 1 });
  const behavior = acsScripts(Object.entries(SCRIPTS).map(([n, code]) => ({ open: false, number: Number(n), code: [...code, PCD.TERMINATE] })), STRINGS);
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior })) },
    { name: "loadacs.txt", data: `${LIBRARY}\n` },
    { name: `acs/${LIBRARY.toLowerCase()}.o`, data: libraryScript() },
    { name: "gldefs.txt", data: GLDEFS },
    ...Object.entries(SHADERS).map(([lump, text]) => ({ name: `shaders/acs/${lump}.fp`, data: text })),
  ]);
}

export const VANTAGES = [{ name: "hall", at: { x: 512, y: 300, z: 0, angle: 90, pitch: 0 } }];

const ref = (name, prev) => [...(prev ? [`shaderenable ${prev} 0`] : []), `shaderenable ${name} 1`, 800];
export const STEPS = [
  { name: "plain", commands: ["crosshair 0", "r_drawplayersprites 0", 1500] },
  { name: "red", commands: ref("red") },
  { name: "yellow", commands: ref("yellow", "red") },
  { name: "blue", commands: ref("blue", "yellow") },
  { name: "white", commands: ref("white", "blue") },
  { name: "green", commands: ref("green", "white") },
  { name: "s1f", commands: ["shaderenable green 0", "puke 1", 800] },
  { name: "s2f", commands: ["puke 2", 800] },
  { name: "s3f", commands: ["puke 3", 800] },
  { name: "s4f", commands: ["puke 4", 800] },
  { name: "s4fNoW", commands: ["puke 5", 800] },
  { name: "s1iDefault", commands: ["puke 6", 800] },
  { name: "s1i", commands: ["puke 7", 800] },
  { name: "ownWins", commands: ["puke 8", 800] },
  { name: "timedOn", commands: ["puke 9", 500] },
  { name: "timedOff", commands: [4000] },
  { name: "refreshedOn", commands: ["puke 11", 2000, "puke 11", 2000] },
  { name: "refreshedOff", commands: [2000] },
  { name: "beforeSave", commands: ["puke 10", 800, "save fuactl_acsshaders.zds", 1500] },
  { name: "cleared", commands: ["shaderenable u3f 0", 800] },
  { name: "loaded", commands: ["load fuactl_acsshaders.zds", 3000] },
  { name: "newLevel", commands: ["map MAP01", 3000] },
  { name: "libraryReturns", commands: ["puke 21", 800] },
];

// [Doruk-Aksoy] Below the console's lines at the top and above the HUD.
const VIEW = [0.0, 0.06, 1.0, 0.8];
export const BOXES = { MAP01: {
  oneFloat: { box: VIEW, pair: ["red", "s1f"], ceiling: 0.5 },
  twoFloats: { box: VIEW, pair: ["yellow", "s2f"], ceiling: 0.5 },
  threeFloats: { box: VIEW, pair: ["blue", "s3f"], ceiling: 0.5 },
  fourFloats: { box: VIEW, pair: ["white", "s4f"], ceiling: 0.5 },
  fourthFloat: { box: VIEW, pair: ["s4f", "s4fNoW"], floor: 100 },
  intDefault: { box: VIEW, pair: ["green", "s1iDefault"], ceiling: 0.5 },
  int: { box: VIEW, pair: ["red", "s1i"], ceiling: 0.5 },
  ownWins: { box: VIEW, pair: ["yellow", "ownWins"], ceiling: 0.5 },
  timedOn: { box: VIEW, pair: ["red", "timedOn"], ceiling: 0.5 },
  timedOff: { box: VIEW, pair: ["plain", "timedOff"], ceiling: 1 },
  refreshedOn: { box: VIEW, pair: ["red", "refreshedOn"], ceiling: 0.5 },
  refreshedOff: { box: VIEW, pair: ["plain", "refreshedOff"], ceiling: 1 },
  beforeSave: { box: VIEW, pair: ["yellow", "beforeSave"], ceiling: 0.5 },
  cleared: { box: VIEW, pair: ["plain", "cleared"], ceiling: 1 },
  loaded: { box: VIEW, pair: ["yellow", "loaded"], ceiling: 0.5 },
  newLevel: { box: VIEW, pair: ["plain", "newLevel"], ceiling: 2 },
  libraryReturns: { box: VIEW, pair: ["green", "libraryReturns"], ceiling: 0.5 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "acsshaders", wad: buildAcsShadersPk3(), maps: ["MAP01"],
  vantages: VANTAGES, steps: STEPS, boxes: BOXES }), compareSequence);
