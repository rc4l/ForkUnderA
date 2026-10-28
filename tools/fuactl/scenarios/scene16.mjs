// [Doruk-Aksoy] The 3D in 16-bit float, as UZDoom's scene buffer is: exit 0 when a post-process shader reads an additive sprite's light past 1 and a subtractive sprite's below 0, both of which an 8-bit scene clamps away.
//
// usage:  node scenarios/scene16.mjs [port]
import { UdmfMap, wad, pk3, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain, compareSequence } from "../src/template.mjs";

const ADD = 20101, SUB = 20102;
const actor = (name, number, style) => `actor ${name} ${number}\n{\n  RenderStyle ${style}\n  Alpha 1.0\n  Scale 6.0\n  +NOGRAVITY\n  States\n  {\n  Spawn:\n    BAL1 A -1 Bright\n    Stop\n  }\n}\n`;
export const DECORATE = actor("ZXSceneAdd", ADD, "Add") + actor("ZXSceneSub", SUB, "Subtract");

const block = (name, lump) => `HardwareShader PostProcess scene\n{\n\tName "${name}"\n\tShader "shaders/s16/${lump}.fp" 330\n}\n`;
export const GLDEFS = block("over", "over") + block("under", "under") + block("black", "black");
export const SHADERS = {
  // [Doruk-Aksoy] Only what stands above 1, four times over.
  over: "void main()\n{\n\tFragColor = vec4(max(texture(InputTexture, TexCoord).rgb - 1.0, 0.0) * 4.0, 1.0);\n}\n",
  // [Doruk-Aksoy] Only what stands below 0, four times over.
  under: "void main()\n{\n\tFragColor = vec4(max(-texture(InputTexture, TexCoord).rgb, 0.0) * 4.0, 1.0);\n}\n",
  black: "void main()\n{\n\tFragColor = vec4(0.0, 0.0, 0.0, 1.0);\n}\n",
};

// [Doruk-Aksoy] One lit hall: a large fireball added over the north wall on the left, another subtracted from it on the right.
export function buildScene16Pk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 224 },
    (edge) => ({ side: { texturemiddle: edge === "north" ? "STARTAN2" : "BROWN1" } }));
  m.thing({ x: 512, y: 300, angle: 90, type: 1 });
  m.thing({ x: 420, y: 560, height: 16, angle: 90, type: ADD });
  m.thing({ x: 604, y: 560, height: 16, angle: 90, type: SUB });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) },
    { name: "decorate.txt", data: DECORATE },
    { name: "gldefs.txt", data: GLDEFS },
    ...Object.entries(SHADERS).map(([lump, text]) => ({ name: `shaders/s16/${lump}.fp`, data: text })),
  ]);
}

export const VANTAGES = [{ name: "hall", at: { x: 512, y: 300, z: 0, angle: 90, pitch: 0 } }];

const on = (name) => `shaderenable ${name} 1`;
const off = (name) => `shaderenable ${name} 0`;
export const STEPS = [
  { name: "plain", commands: ["crosshair 0", "r_drawplayersprites 0", 1500] },
  { name: "over", commands: [on("over"), 800] },
  { name: "under", commands: [off("over"), on("under"), 800] },
  { name: "black", commands: [off("under"), on("black"), 800] },
];

// [Doruk-Aksoy] Around each sprite, below the console's lines.
const LEFT = [0.3, 0.25, 0.5, 0.75];
const RIGHT = [0.5, 0.25, 0.7, 0.75];
export const BOXES = { MAP01: {
  // [Doruk-Aksoy] Measured 14.35 and 51.26 at 16 bits, 0 at 8 bits.
  addedPastOne: { box: LEFT, pair: ["black", "over"], floor: 5 },
  subtractedBelowZero: { box: RIGHT, pair: ["black", "under"], floor: 15 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "scene16", wad: buildScene16Pk3(), maps: ["MAP01"],
  vantages: VANTAGES, steps: STEPS, boxes: BOXES, sides: [{ name: "vk", launchCvars: { fua_dg_scene16: 1 } }] }), compareSequence);
