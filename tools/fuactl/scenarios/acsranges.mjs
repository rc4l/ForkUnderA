// [Doruk-Aksoy] UZDoom's colourise and tint translation ranges (ACS TRANSLATIONRANGE4/5, TRNSLATE's # and @; features/trnslate): exit 0 when every entry
// each one writes holds UZDoom's formula over the game palette, the end index is left out, a range past the palette changes nothing, the TRNSLATE forms
// build the same tables as the ACS ones, and fullbright squares drawn through them show those colours.
//
// usage:  node scenarios/acsranges.mjs              ours: the tables through world.translation, then the picture
//         node scenarios/acsranges.mjs --uzdoom     UZDoom (FUACTL_UZDOOM): the same pk3's picture, against the same formula
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { UdmfMap, wad, mapLumps, pk3, readWad, doomPicture } from "../src/udmf.mjs";
import { acsScripts, PCD, SCRIPT, createTranslation, callSpecial } from "../src/acs.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareEachLaunch, FREEDOOM2 } from "../src/template.mjs";
import { resolveEngine } from "../src/launch.mjs";
import { runUzdoom, newestPng } from "../src/uzdoom.mjs";
import { decodePNG, px } from "../src/pngcore.mjs";
import * as cap from "../src/capture.mjs";
import * as ui from "../src/ui.mjs";

export const STRINGS = ["ranges"];

// [Doruk-Aksoy] The two ranges, each over 16 to 48 with 48 left out, the ACS translation each is built as, and the TRNSLATE name built the same way.
export const COLOURISE = { start: 16, end: 48, rgb: [255, 128, 0], acs: 1, name: "ZxColourise" };
export const TINT = { start: 16, end: 48, rgb: [0, 64, 255], amount: 40, acs: 2, name: "ZxTint" };
// [Doruk-Aksoy] ACS translation 3 gets a palette range and a colourise naming indices past the palette, which UZDoom refuses with a warning, and 4 a colourise
// and then a start with no range at all; both end as the identity.
export const REFUSED = 3, REDEFINED = 4;
export const IDENTITIES = [REFUSED, REDEFINED];
export const TRNSLATE = `${COLOURISE.name} = "${COLOURISE.start}:${COLOURISE.end}=#[${COLOURISE.rgb}]"\n${TINT.name} = "${TINT.start}:${TINT.end}=@${TINT.amount}[${TINT.rgb}]"\n`;

// [Doruk-Aksoy] An ACS translation's value on an actor: TRANSLATION_LevelScripted (4), from 0.
export const acsTranslation = (n) => (4 << 16) | (n - 1);

// [Doruk-Aksoy] A channel as a PalEntry keeps it: the int part's low byte, held inside int first.
const channel = (v) => Math.trunc(Math.max(Math.min(v, 2147483647), -2147483648)) & 0xff;

// [Doruk-Aksoy] UZDoom's AddColourisation for one colour: the target times its luminance.
export function colourise([r, g, b], [tr, tg, tb]) {
  const grey = Math.min((r * 0.299 + g * 0.587 + b * 0.114) / 255, 1);
  return [channel(tr * grey), channel(tg * grey), channel(tb * grey)];
}

// [Doruk-Aksoy] UZDoom's AddTint for one colour, in float as it works it out.
export function tint([r, g, b], [tr, tg, tb], amount) {
  const f = Math.fround;
  const a = f(amount * f(0.01)), ia = f(1 - a);
  const mix = (base, target) => channel(f(f(f(base) * ia) + f(target * a)));
  return [mix(r, tr), mix(g, tg), mix(b, tb)];
}

// [Doruk-Aksoy] What a range leaves in a table's colours, by table index: the game palette's, each entry from start to end - 1 through `f` (at the index the
// palette's order gives it, a later one winning).
export function expectedColours(base, order, { start, end }, f) {
  const out = base.map((c) => [...c]);
  for (let i = start; i < end; i++) out[order[i]] = f(base[i]);
  return out;
}

// [Doruk-Aksoy] The squares, left to right: a palette index each, drawn through a translation, and the colour it should show.
const SQUARE_COLOUR = 20, SQUARE_END = 48, SQUARE_TINT = 30;
export const SQUARES = [
  { name: "ZxSqColourise", frame: "A", index: SQUARE_COLOUR, tid: 21, acs: COLOURISE.acs, want: (base) => colourise(base[SQUARE_COLOUR], COLOURISE.rgb) },
  { name: "ZxSqColouriseEnd", frame: "B", index: SQUARE_END, tid: 22, acs: COLOURISE.acs, want: (base) => base[SQUARE_END] },
  { name: "ZxSqTint", frame: "C", index: SQUARE_TINT, tid: 23, acs: TINT.acs, want: (base) => tint(base[SQUARE_TINT], TINT.rgb, TINT.amount) },
  { name: "ZxSqNamedColourise", frame: "A", index: SQUARE_COLOUR, translation: COLOURISE.name, want: (base) => colourise(base[SQUARE_COLOUR], COLOURISE.rgb) },
  { name: "ZxSqNamedTint", frame: "C", index: SQUARE_TINT, translation: TINT.name, want: (base) => tint(base[SQUARE_TINT], TINT.rgb, TINT.amount) },
];
export const SQUARE_NUM = 30300, SPACING = 96, DISTANCE = 256, START = [256, 64];

export function buildDecorate() {
  return SQUARES.map((s, i) => `actor ${s.name} ${SQUARE_NUM + i}\n{\n\t+NOGRAVITY\n\t+NOINTERACTION\n\tScale 2.0\n${s.translation ? `\tTranslation "${s.translation}"\n` : ""}`
    + `\tStates\n\t{\n\tSpawn:\n\t\tZXSQ ${s.frame} -1 Bright\n\t\tStop\n\t}\n}\n`).join("");
}

// [Doruk-Aksoy] Script 1: the three ACS translations, the squares that use them, then a log that it ran.
export function buildScript() {
  return [...createTranslation(COLOURISE.acs, [{ op: "TRANSLATIONRANGE4", args: [COLOURISE.start, COLOURISE.end, ...COLOURISE.rgb] }]),
    ...createTranslation(TINT.acs, [{ op: "TRANSLATIONRANGE5", args: [TINT.start, TINT.end, TINT.amount, ...TINT.rgb] }]),
    ...createTranslation(REFUSED, [[0, 300, 0, 0], { op: "TRANSLATIONRANGE4", args: [-1, 10, 255, 0, 0] }]),
    ...createTranslation(REDEFINED, [{ op: "TRANSLATIONRANGE4", args: [COLOURISE.start, COLOURISE.end, ...COLOURISE.rgb] }]),
    ...createTranslation(REDEFINED, []),
    ...SQUARES.filter((s) => s.acs).flatMap((s) => callSpecial(SPECIAL.Thing_SetTranslation, [s.tid, s.acs])),
    PCD.BEGINPRINT, PCD.PUSHNUMBER, 0, PCD.PRINTSTRING, PCD.ENDLOG, PCD.TERMINATE];
}

// [Doruk-Aksoy] A room, the player looking north at the squares in a row DISTANCE ahead, each 64 units square with its middle at eye height.
export function buildRangesPk3({ withScripts = (s) => s } = {}) {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: START[0], y: START[1], angle: 90, type: 1 });
  SQUARES.forEach((s, i) => m.thing({ x: START[0] + (i - 2) * SPACING, y: START[1] + DISTANCE, height: 9, angle: 270, type: SQUARE_NUM + i, ...(s.tid ? { id: s.tid } : {}) }));
  const scripts = [{ code: buildScript(), type: SCRIPT.CLOSED, number: 1 }];
  const frames = [...new Set(SQUARES.map((s) => `${s.frame}:${s.index}`))].map((f) => f.split(":"));
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(withScripts(scripts), STRINGS) })) },
    { name: "decorate.txt", data: Buffer.from(buildDecorate(), "latin1") },
    { name: "trnslate.txt", data: Buffer.from(TRNSLATE, "latin1") },
    ...frames.map(([frame, index]) => ({ name: `sprites/ZXSQ${frame}0.lmp`, data: doomPicture(32, 32, Number(index)) }))]);
}

// [Doruk-Aksoy] The game palette's colours from an IWAD's PLAYPAL, for the UZDoom side, which has no world.translation.
export function iwadPalette(file = FREEDOOM2) {
  const pal = readWad(fs.readFileSync(file)).find((l) => l.name === "PLAYPAL").data;
  return Array.from({ length: 256 }, (_, i) => [pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]]);
}

const rgbOf = (v) => [(v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff];
const same = (a, b) => a[0] === b[0] && a[1] === b[1] && a[2] === b[2];

// [Doruk-Aksoy] What is wrong with ours' tables, each read by world.translation: the two ranges' every entry, the refused and started-again ones the identity,
// each TRNSLATE twin equal.
export async function judgeTables(read) {
  const faults = [];
  const acs = async (n) => read({ translation: acsTranslation(n) });
  for (const range of [COLOURISE, TINT]) {
    const table = await acs(range.acs);
    if (!table) { faults.push(`ACS translation ${range.acs} reads nothing`); continue; }
    const base = table.base.map(rgbOf);
    const f = range === TINT ? (c) => tint(c, range.rgb, range.amount) : (c) => colourise(c, range.rgb);
    const want = expectedColours(base, table.order, range, f);
    const wrong = table.palette.map(rgbOf).flatMap((c, j) => (same(c, want[j]) ? [] : [`${j} ${c} not ${want[j]}`]));
    if (wrong.length) faults.push(`ACS translation ${range.acs} has ${wrong.length} entries off UZDoom's formula: ${wrong.slice(0, 3).join(", ")}`);
    const named = await read({ name: range.name });
    if (!named) faults.push(`${range.name} reads nothing`);
    else if (named.palette.join() !== table.palette.join() || named.remap.join() !== table.remap.join()) faults.push(`${range.name} differs from ACS translation ${range.acs}`);
  }
  for (const n of IDENTITIES) {
    const table = await acs(n);
    if (!table) faults.push(`ACS translation ${n} reads nothing`);
    else if (table.palette.join() !== table.base.join() || table.remap.some((v, i) => v !== i)) faults.push(`ACS translation ${n} is not the identity`);
  }
  return faults;
}

// [Doruk-Aksoy] Where each square's middle falls in a w by h picture at Doom's 90-degree view widened to the picture's shape.
export function squarePoints(w, h) {
  const tanHalf = Math.max(1, (w / h) / (4 / 3));
  return SQUARES.map((_, i) => [Math.round(w / 2 + ((i - 2) * SPACING * (w / 2)) / (DISTANCE * tanHalf)), Math.round(h / 2)]);
}

// [Doruk-Aksoy] The middle colour of a 5x5 patch, channel by channel, so an edge or a stray pixel can't decide it.
export function patchColour(img, [x, y]) {
  const got = [];
  for (let dy = -2; dy <= 2; dy++) for (let dx = -2; dx <= 2; dx++) got.push(px(img, x + dx, y + dy));
  return [0, 1, 2].map((k) => got.map((c) => c[k]).sort((a, b) => a - b)[12]);
}

// [Doruk-Aksoy] How far a drawn colour may be from its table's: a renderer's own rounding.
export const PICTURE_TOLERANCE = 3;

// [Doruk-Aksoy] What is wrong with a picture of the squares, each against its colour over `base`.
export function judgePicture(img, base) {
  return squarePoints(img.w, img.h).flatMap((p, i) => {
    const got = patchColour(img, p), want = SQUARES[i].want(base);
    return got.every((v, k) => Math.abs(v - want[k]) <= PICTURE_TOLERANCE) ? [] : [`${SQUARES[i].name} drew ${got} at ${p}, not ${want}`];
  });
}

// [Doruk-Aksoy] Hides the weapon and crosshair, which would sit over the middle square.
export const CLEAN_VIEW = ["r_drawplayersprites 0", "crosshair 0"];

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  for (const line of CLEAN_VIEW) await cap.exec(c, line);
  const log = await cap.exec(c, "puke 1", { quietMs: 400 });
  const faults = /ranges/.test(log) ? [] : [`script 1 never logged (${String(log).trim().slice(0, 160)})`];
  faults.push(...await judgeTables((args) => c.rpc("world.translation", args).catch(() => null)));
  await cap.waitTics(c, 10);
  const table = await c.rpc("world.translation", { translation: acsTranslation(COLOURISE.acs) }).catch(() => null);
  const shot = await ui.screenshot(c, resolveEngine(), "acsranges");
  faults.push(...judgePicture(decodePNG(shot.path), table ? table.base.map(rgbOf) : iwadPalette()));
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: colourise and tint write UZDoom's formula over ${COLOURISE.end - COLOURISE.start} entries each, the end left out, a range past the palette changes nothing, a translation started again with no range is the identity, TRNSLATE's # and @ build the same tables, and the squares draw them`, failed: faults.length ? 1 : 0 };
}

export const SIDES = [{ name: "vk" }];

// [Doruk-Aksoy] The UZDoom side: script 1 puked, the view cleared, then a picture.
export const UZDOOM_CHAIN = `wait 35;${CLEAN_VIEW.join(";")};puke 1;wait 35;screenshot;wait 35;quit`;

if (process.argv.includes("--uzdoom") && process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "acsranges-uzdoom-"));
  const pk3File = path.join(dir, "acsranges.pk3");
  fs.writeFileSync(pk3File, buildRangesPk3());
  const log = runUzdoom({ pk3: pk3File, map: "MAP01", chain: UZDOOM_CHAIN, dir });
  const png = newestPng(dir);
  const faults = [...(/ranges/.test(log) ? [] : ["script 1 never logged"]), ...(png ? judgePicture(decodePNG(png), iwadPalette()) : ["UZDoom took no picture"])];
  console.log(faults.length ? `FAIL UZDoom: ${faults.join("; ")}` : `PASS UZDoom: every square draws the colour UZDoom's formula gives (${dir})`);
  process.exit(faults.length ? 1 : 0);
} else {
  runScenarioIfMain(import.meta.url, () => ({ name: "acsranges", wad: buildRangesPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
}
