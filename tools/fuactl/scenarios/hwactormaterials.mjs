// [Doruk-Aksoy] GLDEFS fua_Material on actors on Vulkan, three of one DECORATE class in a row: exit 0 when they start on its material, a script's uniforms reach only the actor named, a vec3 and an int land, the calls return what they should, a savegame keeps each actor's own values and a new level starts them over, set against materials of one colour each.
//
// usage:  node scenarios/hwactormaterials.mjs [port]
import { UdmfMap, wad, pk3, mapLumps } from "../src/udmf.mjs";
import { acsScripts, AcsScript, PCD, callFunc } from "../src/acs.mjs";
import { runScenarioIfMain, compareSequence } from "../src/template.mjs";
import { solid } from "./hwshaderparity.mjs";

const fill = (rgb) => `vec4 ProcessTexel()\n{\n\treturn vec4(${rgb}, 1.0);\n}\n`;
export const SHADERS = {
  glow: fill("swap == 1 ? tint.bgr : tint"),
  red: fill("1.0, 0.0, 0.0"),
  green: fill("0.0, 1.0, 0.0"),
  blue: fill("0.0, 0.0, 1.0"),
  white: fill("1.0, 1.0, 1.0"),
};

export const GLDEFS = `fua_Material "zxglow" { Shader "shaders/glow.fp" fua_Uniform vec3 tint = 1.0 0.0 0.0 fua_Uniform int swap = 0 }
${["red", "green", "blue", "white"].map((c) => `fua_Material "zx${c}" { Shader "shaders/${c}.fp" }`).join("\n")}
`;

const DECORATE = `Actor ZXGlowActor 20140
{
  Radius 16
  Height 56
  +NOGRAVITY
  +NOINTERACTION
  fua_Material "zxglow"
  States
  {
  Spawn:
    ZXAS A -1
    Stop
  }
}
`;

// [Doruk-Aksoy] The ACS functions by number, as fua.acs declares them.
const F = { material: 24006, uniform1f: 24007, uniform3f: 24009, uniform1i: 24011 };
export const STRINGS = ["zxglow", "zxred", "zxgreen", "zxblue", "zxwhite", "tint", "swap", "nosuch"];
const s = (name) => STRINGS.indexOf(name);
const fx = (v) => Math.round(v * 65536);
const material = (tid, name) => callFunc(F.material, [tid, s(name)]);
const tint = (tid, r, g, b) => callFunc(F.uniform3f, [tid, s("tint"), fx(r), fx(g), fx(b)]);
const drop = (words) => [...words, PCD.DROP];

// [Doruk-Aksoy] Script 5: actor 1's tint white when every call returns what it should, else it stays red.
export function returnsScript() {
  const a = new AcsScript();
  const expect = (words, want) => a.emit(...words, PCD.PUSHNUMBER, want, PCD.EQ).jump(PCD.IFNOTGOTO, "fail");
  a.emit(...drop(material(1, "zxglow")));
  expect(material(99, "zxglow"), 0);
  expect(material(1, "nosuch"), 0);
  expect(callFunc(F.uniform1f, [1, s("nosuch"), fx(1)]), 0);
  expect(callFunc(F.uniform1f, [1, s("tint")]), 0);
  expect(tint(1, 1, 0, 0), 1);
  expect(tint(0, 1, 0, 0), 0);
  a.emit(...drop(tint(1, 1, 1, 1)), PCD.TERMINATE);
  a.label("fail").emit(PCD.TERMINATE);
  return a.words();
}

// [Doruk-Aksoy] The closed scripts the steps puke, script 5 first since its jumps count from the lump's first word.
export const SCRIPTS = [
  { open: false, number: 5, code: returnsScript() },
  { open: false, number: 1, code: [...drop(material(1, "zxred")), ...drop(material(2, "zxgreen")), ...drop(material(3, "zxblue")), PCD.TERMINATE] },
  { open: false, number: 2, code: [...[1, 2, 3].flatMap((t) => drop(material(t, "zxglow"))), PCD.TERMINATE] },
  { open: false, number: 3, code: [...drop(tint(2, 0, 1, 0)), ...drop(tint(3, 0, 0, 1)), PCD.TERMINATE] },
  { open: false, number: 4, code: [...drop(material(1, "zxwhite")), PCD.TERMINATE] },
  { open: false, number: 6, code: [...drop(callFunc(F.uniform1i, [3, s("swap"), 1])), PCD.TERMINATE] },
];

// [Doruk-Aksoy] A fully lit room, three actors in a row before the player, tids 1 to 3 left to right.
export function buildHwActorMaterialsPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 255 },
    () => ({ side: { texturemiddle: "BROWN1" } }));
  [160, 256, 352].forEach((x, i) => m.thing({ x, y: 320, angle: 270, type: 20140, id: i + 1 }));
  m.thing({ x: 256, y: 100, angle: 90, type: 1 });
  const behavior = acsScripts(SCRIPTS, STRINGS);
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior })) },
    { name: "gldefs.txt", data: GLDEFS },
    { name: "decorate.txt", data: DECORATE },
    ...Object.entries(SHADERS).map(([name, src]) => ({ name: `shaders/${name}.fp`, data: src })),
    { name: "sprites/ZXASA0.png", data: solid([128, 128, 128], 48, [24, 48]) },
  ]);
}

export const VANTAGES = [{ name: "row", at: { x: 256, y: 100, z: 0, angle: 90, pitch: 0 } }];

export const STEPS = [
  { name: "plain", commands: ["crosshair 0", "r_drawplayersprites 0", 1500] },
  { name: "refRGB", commands: ["puke 1", 800] },
  { name: "refWhite", commands: ["puke 4", 800] },
  { name: "back", commands: ["puke 2", 800] },
  { name: "set", commands: ["puke 3", 800] },
  { name: "swapped", commands: ["puke 6", 800] },
  { name: "returns", commands: ["puke 5", 800] },
  { name: "beforeSave", commands: ["save fuactl_hwactormaterials.zds", 1500] },
  { name: "newLevel", commands: ["map MAP01", 3000] },
  { name: "loaded", commands: ["load fuactl_hwactormaterials.zds", 3000] },
];

// [Doruk-Aksoy] Each actor's middle, left to right.
export const ACTOR_BOXES = [[0.3, 0.45, 0.34, 0.55], [0.49, 0.45, 0.51, 0.55], [0.66, 0.45, 0.7, 0.55]];
const check = (actor, ref, step, ceiling = 2) => ({ box: ACTOR_BOXES[actor], pair: [ref, step], ceiling });
export const BOXES = { MAP01: {
  startsRed: check(0, "refRGB", "plain"),
  backToDefaults: check(2, "plain", "back"),
  setGreen: check(1, "refRGB", "set"),
  setBlue: check(2, "refRGB", "set"),
  othersUntouched: check(0, "plain", "set"),
  intSwaps: check(2, "plain", "swapped"),
  returnsWhite: check(0, "refWhite", "returns"),
  newLevelRed: check(1, "plain", "newLevel"),
  loadedGreen: check(1, "refRGB", "loaded"),
  loadedWhite: check(0, "refWhite", "loaded"),
  loadedSwapped: check(2, "plain", "loaded"),
} };

runScenarioIfMain(import.meta.url, () => ({ name: "hwactormaterials", wad: buildHwActorMaterialsPk3(), maps: ["MAP01"],
  vantages: VANTAGES, steps: STEPS, boxes: BOXES }), compareSequence);
