// [Doruk-Aksoy] TRNSLATE's named translations against the same tables written inline: exit 0 when every named, based, palette-lump and A_SetTranslation actor draws as its inline twin.
//
// usage:  node scenarios/trnslate.mjs [port]
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { runScenarioIfMain } from "../src/template.mjs";

// [Doruk-Aksoy] Every definition kind UZDoom's parser takes: plain, on a named base, on a standard one, and the three palette lump formats.
export const TRNSLATE = `// [Doruk-Aksoy] fuactl scenario trnslate
ZxRed = "0:255=[255,0,0]:[255,0,0]"
ZxBlueTop : ZxRed = "0:127=[0,0,255]:[0,0,255]"
/* the standard base: Doom's green ramp to red, then the darkest sixteen green */
ZxStd : 2 = "0:15=[0,255,0]:[0,255,0]"
ZxJasc = "ZXJASC", 1
ZxRaw = "ZXRAW"
ZxPng = "ZXPNG", 112
`;

// [Doruk-Aksoy] Each actor's DECORATE body on MAP01 and its inline twin's on MAP02, by the position it stands in.
export const ACTORS = [
  { name: "ZxNamedRed", body: 'Translation "ZxRed"', twin: '"0:255=[255,0,0]:[255,0,0]"' },
  { name: "ZxNamedBase", body: 'Translation "ZxBlueTop"', twin: '"0:255=[255,0,0]:[255,0,0]", "0:127=[0,0,255]:[0,0,255]"' },
  { name: "ZxNamedStd", body: 'Translation "ZxStd"', twin: '"112:127=32:47", "0:15=[0,255,0]:[0,255,0]"' },
  { name: "ZxJascPal", body: 'Translation "ZxJasc"', twin: '"1:255=[0,255,255]:[0,255,255]"' },
  { name: "ZxRawPal", body: 'Translation "ZxRaw"', twin: '"0:255=[255,0,255]:[255,0,255]"' },
  { name: "ZxPngPal", body: 'Translation "ZxPng"', twin: '"112:127=[255,128,0]:[255,128,0]"' },
  { name: "ZxSetNamed", states: ['PLAY A 1 A_SetTranslation("ZxRed")'], twin: '"0:255=[255,0,0]:[255,0,0]"' },
  { name: "ZxSetReset", body: 'Translation "ZxRed"', states: ['PLAY A 1 A_SetTranslation("ZxBlueTop")', 'PLAY A 1 A_SetTranslation("")'],
    twin: '"0:255=[255,0,0]:[255,0,0]"' },
  { name: "ZxSetUnknown", body: 'Translation "ZxRed"', states: ['PLAY A 1 A_SetTranslation("ZxNope")'], twin: '"0:255=[255,0,0]:[255,0,0]"' },
];

const NAMED_NUM = 30100, TWIN_NUM = 30200, SPACING = 56, ROW_Y = 456;

// [Doruk-Aksoy] A PLAY-sprite actor that holds its first frame, runs its states' actions a tic apart, then stands.
function actorDecorate(name, num, body, states = []) {
  return `actor ${name} ${num}\n{\n\t+NOGRAVITY\n\t${body || ""}\n\tStates\n\t{\n\tSpawn:\n\t\tPLAY A 1\n${states.map((s) => `\t\t${s}\n`).join("")}\t\tPLAY A -1\n\t\tStop\n\t}\n}\n`;
}

export function buildDecorate() {
  return ACTORS.map((a, i) => actorDecorate(a.name, NAMED_NUM + i, a.body, a.states)
    + actorDecorate(`${a.name}Twin`, TWIN_NUM + i, `Translation ${a.twin}`)).join("");
}

// [Doruk-Aksoy] count copies of one RGB triplet.
function colours(count, [r, g, b]) {
  return Buffer.from(Array.from({ length: count }, () => [r, g, b]).flat());
}

// [Doruk-Aksoy] A 1x1 paletted PNG whose PLTE holds sixteen orange colours (CRCs zero, which the reader never checks).
export function pngPalette() {
  const chunk = (id, data) => {
    const head = Buffer.alloc(8);
    head.writeUInt32BE(data.length, 0);
    head.write(id, 4, "latin1");
    return Buffer.concat([head, data, Buffer.alloc(4)]);
  };
  const ihdr = Buffer.from([0, 0, 0, 1, 0, 0, 0, 1, 8, 3, 0, 0, 0]);
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk("IHDR", ihdr),
    chunk("PLTE", colours(16, [255, 128, 0])), chunk("IEND", Buffer.alloc(0))]);
}

export function jascPalette() {
  return `JASC-PAL\r\n0100\r\n255\r\n${"0 255 255\r\n".repeat(255)}`;
}

// [Doruk-Aksoy] The row of actors, the first class of each pair on MAP01 and its twin on MAP02.
function row(firstNum) {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 1024, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5",
    lightlevel: 255 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 512, y: 100, angle: 90, type: 1 });
  ACTORS.forEach((_, i) => m.thing({ x: 512 + (i - (ACTORS.length - 1) / 2) * SPACING, y: ROW_Y, angle: 270, type: firstNum + i }));
  return m;
}

export function buildTrnslateWad() {
  return wad([...mapLumps("MAP01", row(NAMED_NUM)), ...mapLumps("MAP02", row(TWIN_NUM)),
    { name: "TRNSLATE", data: TRNSLATE }, { name: "DECORATE", data: buildDecorate() },
    { name: "ZXJASC", data: jascPalette() }, { name: "ZXRAW", data: colours(256, [255, 0, 255]) }, { name: "ZXPNG", data: pngPalette() }]);
}

// [Doruk-Aksoy] From the room's south side, the whole row in view.
export const VANTAGES = [{ name: "row", at: { x: 512, y: 200, z: 0, angle: 90, pitch: 0 } }];

// [Doruk-Aksoy] One box per actor, so a failing one is named; the row spans about the middle two thirds of the frame.
export const BOXES = {
  MAP01: Object.fromEntries(ACTORS.map((a, i) => {
    const centre = 0.5 + ((i - (ACTORS.length - 1) / 2) * SPACING / (ROW_Y - 200)) * 0.375;
    return [a.name, { box: [centre - 0.035, 0.3, centre + 0.035, 0.75], ceiling: 0.05 }];
  })),
};

// [Doruk-Aksoy] The inline twins are what each named translation must draw, so the second side loads MAP02.
export const SIDES = [{ name: "named" }, { name: "inline", commands: ["map MAP02"] }];

runScenarioIfMain(import.meta.url, () => ({ name: "trnslate", wad: buildTrnslateWad(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
