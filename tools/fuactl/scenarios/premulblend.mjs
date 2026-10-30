// [Doruk-Aksoy] An additive and a translucent column and pane before a grey wall, Vulkan with fua_dg_premulblend off, on, and on with the additive column made translucent: exit 0 when on draws what off does and the additive column stays apart from a translucent one.
//
// usage:  node scenarios/premulblend.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [Doruk-Aksoy] One column drawn additively and one translucently, at the same alpha and three times their size.
export const ADD_EDNUM = 15400, TRANS_EDNUM = 15401;
export function premulDecorate() {
  const column = (name, ednum, style) => `actor ${name} : ShortGreenColumn ${ednum}\n{\n  RenderStyle ${style}\n  Alpha 0.6\n  Scale 3\n}\n`;
  return { name: "DECORATE", data: column("ZXPremulAdd", ADD_EDNUM, "Add") + column("ZXPremulTrans", TRANS_EDNUM, "Translucent") };
}

// [Doruk-Aksoy] The room, its left column additive when `leftAdditive`, with a pane at half alpha each side, the left one additive.
function premulRoom(leftAdditive) {
  const m = new UdmfMap();
  const props = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
  const room = m.room(0, 0, 1024, 1024, props, (edge) => ({ side: { texturemiddle: edge === "north" ? "STARTAN2" : "BROWN1" } }));
  for (const [x0, style] of [[32, "add"], [800, "translucent"]]) {
    const pane = (edge) => (edge === "south" ? { texturemiddle: "BRICK1" } : {});
    m.block(x0, 880, x0 + 192, 896, props, room, pane, pane, (edge) => (edge === "south" ? { alpha: 0.5, renderstyle: style } : {}));
  }
  m.thing({ x: 416, y: 600, angle: 270, type: leftAdditive ? ADD_EDNUM : TRANS_EDNUM });
  m.thing({ x: 608, y: 600, angle: 270, type: TRANS_EDNUM });
  m.thing({ x: 512, y: 200, angle: 90, type: 1 });
  return m;
}

// [Doruk-Aksoy] MAP01 is the scene; MAP02 is it with the additive column translucent, what the additive one must stay apart from.
export function buildPremulBlendWad() {
  return wad([premulDecorate(), ...mapLumps("MAP01", premulRoom(true)), ...mapLumps("MAP02", premulRoom(false))]);
}

export const VANTAGES = [{ name: "front", at: { x: 512, y: 200, z: 0, angle: 90, pitch: 0 } }];

// [Doruk-Aksoy] Off sets the merge's own pipelines aside once the level is up, so it also covers a change of the setting in play.
export const SIDES = [
  { name: "off", launchCvars: { con_notifylines: 0 }, cvars: { fua_dg_premulblend: 0 } },
  { name: "on", launchCvars: { con_notifylines: 0 }, cvars: { fua_dg_premulblend: 1 } },
  { name: "trans", launchCvars: { con_notifylines: 0 }, cvars: { fua_dg_premulblend: 1 }, commands: ["map MAP02"] },
];

// [Doruk-Aksoy] Per pixel, since the merge moves a colour by at most one level; the additive column against a translucent one must differ by far more.
const ADD_COLUMN = [0.355, 0.33, 0.46, 0.59];
export const BOXES = { MAP01: {
  scene: { box: [0, 0.07, 1, 1], block: 1, ceiling: 0.5, pair: ["off", "on"] },
  addcol: { box: ADD_COLUMN, block: 1, ceiling: 1, pair: ["off", "on"] },
  transcol: { box: [0.535, 0.33, 0.64, 0.59], block: 1, ceiling: 1, pair: ["off", "on"] },
  addpane: { box: [0.235, 0.245, 0.34, 0.4], block: 1, ceiling: 1, pair: ["off", "on"] },
  transpane: { box: [0.66, 0.245, 0.765, 0.4], block: 1, ceiling: 1, pair: ["off", "on"] },
  apart: { box: ADD_COLUMN, block: 1, floor: 10, pair: ["on", "trans"] },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "premulblend", wad: buildPremulBlendWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
