// [Doruk-Aksoy] HUD draws through SBARINFO and ACS, Vulkan against itself: exit 0 when a font string, a number and a translucent image the status bar draws after an image anchored at the right edge (DnD's credit panel) match the same draws made before it, and when ACS's HudMessage text, translucent text and font image all show.
//
// usage:  node scenarios/huddraws.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { encodePNG } from "../src/pngcore.mjs";
import { runScenarioIfMain, MUTE } from "../src/template.mjs";
import { acs0, hudMessage, PCD } from "../src/acs.mjs";

const solid = (w, h, rgb) => { const px = Buffer.alloc(w * h * 3); for (let i = 0; i < w * h; i++) px.set(rgb, i * 3); return encodePNG(w, h, px); };

// [Doruk-Aksoy] A digit font from graphics/ as DnD's SHOPFONT is, each glyph an orange cell.
const GLYPHS = [..."0123456789"].map((c) => ({ c, lump: `ZXNUM${c}` })).concat([{ c: "$", lump: "ZXNUMD" }]);
const FONTDEFS = `ZXFONT\n{\n${GLYPHS.map((g) => `  ${g.c} ${g.lump}`).join("\n")}\n}\n`;

// [Doruk-Aksoy] The panel anchored at the right edge first and the text, number and translucent image after it, as DnD draws its credits; holding ZXHudOracle draws the same with the panel last.
// [Doruk-Aksoy] The string does not start with $, which SBARINFO would look up in LANGUAGE.
const AFTER = [
  'drawstring ZXFONT, Untranslated, "0123$", -40, 40, 0, alignment(right);',
  "drawnumber 5, ZXFONT, Untranslated, health, alignment(right), fillzeros, -40, 70;",
  'alpha 0.5 { drawimage "ZXSQUARE", -100, 110; }',
];
const PANEL = 'drawimage "ZXPANEL", -256, 0;';
const body = () => `  InInventory ZXHudOracle\n  {\n    ${[...AFTER, PANEL].join("\n    ")}\n  }\n  else\n  {\n    ${[PANEL, ...AFTER].join("\n    ")}\n  }\n`;
export const SBARINFO = `height 0;\nstatusbar fullscreen, fullscreenoffsets\n{\n${body()}}\nstatusbar normal, fullscreenoffsets\n{\n${body()}}\n`;

const DECORATE = "Actor ZXHudOracle : Inventory\n{\n  Inventory.MaxAmount 1\n}\n";

// [Doruk-Aksoy] MAP01's OPEN script leaves three HudMessages up: BigFont text, BigFont text at alpha 0.5, and a font image at alpha 0.75.
export const ACS_AT = { text: { x: 0.2, y: 0.4 }, half: { x: 0.4, y: 0.4 }, image: { x: 0.6, y: 0.4 } };
function hudScript() {
  const strings = ["ACS", "HALF", "ZXSQUARE", "A", "BIGFONT"];
  return acs0([
    PCD.PUSHNUMBER, 4, PCD.SETFONT,
    ...hudMessage(0, { id: 10, color: 9, hold: 0, ...ACS_AT.text }),
    ...hudMessage(1, { id: 11, color: 9, hold: 0, alpha: 0.5, ...ACS_AT.half }),
    PCD.PUSHNUMBER, 2, PCD.SETFONT, ...hudMessage(3, { id: 12, hold: 0, alpha: 0.75, ...ACS_AT.image }),
    PCD.TERMINATE,
  ], strings);
}

// [Doruk-Aksoy] One lit room, the player facing its north wall; MAP02 is the same room with no script.
function room() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 160 },
    () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 128, angle: 90, type: 1 });
  return m;
}

export function buildHudDrawsPk3() {
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", room(), { behavior: hudScript() })) },
    { name: "maps/MAP02.wad", data: wad(mapLumps("MAP02", room())) },
    { name: "sbarinfo.txt", data: SBARINFO },
    { name: "fontdefs.txt", data: FONTDEFS },
    { name: "decorate.txt", data: DECORATE },
    { name: "graphics/ZXPANEL.png", data: solid(32, 16, [64, 64, 64]) },
    { name: "graphics/ZXSQUARE.png", data: solid(16, 16, [0, 255, 0]) },
    ...GLYPHS.map((g) => ({ name: `graphics/${g.lump}.png`, data: solid(8, 12, [255, 140, 0]) })),
  ]);
}

export const VANTAGES = [{ name: "north", at: { x: 256, y: 128, z: 0, angle: 90, pitch: 0 } }];

// [Doruk-Aksoy] Unscaled, so the status bar's offsets are pixels of the 1280x720 frame, with DnD's screenblocks and no crosshair in the middle.
const HUD = { ...MUTE, hud_scale: 0, screenblocks: 10, crosshair: 0 };
export const SIDES = [
  { name: "after", launchCvars: HUD },
  { name: "oracle", launchCvars: HUD, commands: ["give ZXHudOracle"] },
  { name: "bare", launchCvars: HUD, commands: ["map MAP02"] },
  { name: "nohud", launchCvars: { ...HUD, screenblocks: 12 } },
];

// [Doruk-Aksoy] Each status bar draw where it lands right of x 1280 - 40 and below the panel, judged against the safe order and against no status bar, and each HudMessage around its point.
// [Doruk-Aksoy] A translucent draw is held below what it reads drawn opaque (the image 25.9, the text 7.3), since a corrupt alpha that comes out positive draws it opaque.
const acsBox = ({ x, y }, half) => [x - half, y - 0.03, x + half, y + 0.03];
const SBAR_AT = { String: [0.90, 0.045, 0.98, 0.085], Number: [0.90, 0.09, 0.98, 0.125], AlphaImage: [0.915, 0.145, 0.94, 0.18] };
const SHOWN_CEILING = { AlphaImage: 19 };
export const BOXES = { MAP01: {
  ...Object.fromEntries(Object.entries(SBAR_AT).flatMap(([n, box]) => [
    [`sbar${n}`, { box, pair: ["after", "oracle"], ceiling: 1, block: 4 }],
    [`sbar${n}Shown`, { box, pair: ["after", "nohud"], ceiling: SHOWN_CEILING[n] ?? 255, floor: 5, block: 4 }],
  ])),
  acsText: { box: acsBox(ACS_AT.text, 0.05), pair: ["after", "bare"], ceiling: 255, floor: 5, block: 4 },
  acsHalfText: { box: acsBox(ACS_AT.half, 0.06), pair: ["after", "bare"], ceiling: 5.5, floor: 3, block: 4 },
  acsImage: { box: acsBox(ACS_AT.image, 0.015), pair: ["after", "bare"], ceiling: 255, floor: 5, block: 4 },
} };

runScenarioIfMain(import.meta.url, () => ({ name: "huddraws", wad: buildHudDrawsPk3(), maps: ["MAP01"],
  vantages: VANTAGES, boxes: BOXES, sides: SIDES }));
