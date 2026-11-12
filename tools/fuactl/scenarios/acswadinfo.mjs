// [rc4l] ACS's GetWadInfo and LumpGetInfo's namespace and wad (zandronum@57fe0dc1ae24): exit 0 when a script reads a sprite's namespace, the pk3 holding it, that pk3's name, and an embedded wad's parent, and asks an out-of-range wad whether it is optional or authenticated (@57fd93e4e1a0) and an out-of-range lump its name (@9f4728f2fdf4) without a crash.
//
// usage:  node scenarios/acswadinfo.mjs
import path from "node:path";
import { Buffer } from "node:buffer";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { encodePNG } from "../src/pngcore.mjs";
import { acsScripts, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
// [rc4l] LumpGetInfo's and GetWadInfo's info types, and LumpOpen's full-path flag, as p_acs.cpp numbers them.
export const LUMP_INFO = { NAME: 1, NAMESPACE: 2, WAD: 3 };
export const WAD_INFO = { NAME: 0, PARENTWAD: 3, OPTIONAL: 5, HASAUTHENTICATEDLUMPS: 6 };
// [rc4l] A wad index far past any loaded, whose entry would lie in unmapped memory.
export const BAD_INDEX = 0x40000000;
const LUMP_OPEN_FULLPATH = 1;
// [rc4l] ns_sprites in w_wad.h.
export const NS_SPRITES = 1;
export const SPRITE_PATH = "sprites/zxspa0.png", INNER_WAD = "zxinner.wad", INNER_LUMP = "ZXINNER";

export const STRINGS = [SPRITE_PATH, INNER_LUMP, "ns ", " wad ", " name ", " parent ", " innerwad ", " innerparent ", "optional ", " auth ", "lumpname [", "]"];
const S = Object.fromEntries(STRINGS.map((s, i) => [s.trim(), i]));
const SPRITE_VAR = 0, INNER_VAR = 1;

const solid = (rgb, n = 32) => { const px = Buffer.alloc(n * n * 3); for (let i = 0; i < n * n; i++) px.set(rgb, i * 3); return encodePNG(n, n, px, { grab: [n / 2, n] }); };
const text = (i) => [PCD.PUSHNUMBER, i, PCD.PRINTSTRING];
const lumpInfo = (v, type) => [PCD.PUSHMAPVAR, v, PCD.PUSHNUMBER, type, PCD.CALLFUNC, 2, ACSF.LumpGetInfo];
const wadInfo = (wadWords, type) => [...wadWords, PCD.PUSHNUMBER, type, PCD.CALLFUNC, 2, ACSF.GetWadInfo];

// [rc4l] Script 1 logs the sprite's namespace and wad, that wad's name and parent, then the embedded wad's index and parent.
export function buildScript() {
  return [
    PCD.PUSHNUMBER, S[SPRITE_PATH], PCD.PUSHNUMBER, -1, PCD.PUSHNUMBER, LUMP_OPEN_FULLPATH, PCD.CALLFUNC, 3, ACSF.LumpOpen, PCD.ASSIGNMAPVAR, SPRITE_VAR,
    PCD.PUSHNUMBER, S[INNER_LUMP], PCD.CALLFUNC, 1, ACSF.LumpOpen, PCD.ASSIGNMAPVAR, INNER_VAR,
    PCD.BEGINPRINT,
    ...text(S.ns), ...lumpInfo(SPRITE_VAR, LUMP_INFO.NAMESPACE), PCD.PRINTNUMBER,
    ...text(S.wad), ...lumpInfo(SPRITE_VAR, LUMP_INFO.WAD), PCD.PRINTNUMBER,
    ...text(S.name), ...wadInfo(lumpInfo(SPRITE_VAR, LUMP_INFO.WAD), WAD_INFO.NAME), PCD.PRINTSTRING,
    ...text(S.parent), ...wadInfo(lumpInfo(SPRITE_VAR, LUMP_INFO.WAD), WAD_INFO.PARENTWAD), PCD.PRINTNUMBER,
    ...text(S.innerwad), ...lumpInfo(INNER_VAR, LUMP_INFO.WAD), PCD.PRINTNUMBER,
    ...text(S.innerparent), ...wadInfo(lumpInfo(INNER_VAR, LUMP_INFO.WAD), WAD_INFO.PARENTWAD), PCD.PRINTNUMBER,
    PCD.ENDLOG, PCD.TERMINATE,
  ];
}

// [rc4l] Script 2 logs whether the out-of-range wad is optional and holds authenticated lumps.
export function buildOutOfRangeScript() {
  const bad = [PCD.PUSHNUMBER, BAD_INDEX];
  return [PCD.BEGINPRINT, ...text(S.optional), ...wadInfo(bad, WAD_INFO.OPTIONAL), PCD.PRINTNUMBER,
    ...text(S.auth), ...wadInfo(bad, WAD_INFO.HASAUTHENTICATEDLUMPS), PCD.PRINTNUMBER, PCD.ENDLOG, PCD.TERMINATE];
}

// [rc4l] Script 3 logs the out-of-range lump's name between brackets.
export function buildLumpNameScript() {
  return [PCD.BEGINPRINT, ...text(S["lumpname ["]), PCD.PUSHNUMBER, BAD_INDEX, PCD.PUSHNUMBER, LUMP_INFO.NAME, PCD.CALLFUNC, 2, ACSF.LumpGetInfo, PCD.PRINTSTRING,
    ...text(S["]"]), PCD.ENDLOG, PCD.TERMINATE];
}

export function buildAcsWadInfoPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: buildScript(), type: SCRIPT.CLOSED, number: 1 }, { code: buildOutOfRangeScript(), type: SCRIPT.CLOSED, number: 2 }, { code: buildLumpNameScript(), type: SCRIPT.CLOSED, number: 3 }], STRINGS) })) },
    { name: SPRITE_PATH, data: solid([0, 200, 255]) },
    { name: INNER_WAD, data: wad([{ name: INNER_LUMP, data: Buffer.from("zx") }]) },
  ]);
}

// [rc4l] What is wrong with script 1's log for a pk3 named pk3Name.
export function judgeWadInfo(log, pk3Name) {
  const m = /ns (-?\d+) wad (-?\d+) name (\S*) parent (-?\d+) innerwad (-?\d+) innerparent (-?\d+)/.exec(String(log));
  if (!m) return [`script 1 logged no wad info (${String(log).trim().slice(0, 120)})`];
  const [ns, wadNum, name, parent, inner, innerParent] = [Number(m[1]), Number(m[2]), m[3], Number(m[4]), Number(m[5]), Number(m[6])];
  const faults = [];
  if (ns !== NS_SPRITES) faults.push(`the sprite's namespace is ${ns}, not ${NS_SPRITES}`);
  if (wadNum < 0) faults.push(`the sprite's wad is ${wadNum}`);
  if (name.toLowerCase() !== pk3Name.toLowerCase()) faults.push(`its wad is named "${name}", not "${pk3Name}"`);
  if (parent !== -1) faults.push(`the pk3's parent is ${parent}, not -1`);
  if (inner < 0 || inner === wadNum) faults.push(`the embedded wad is ${inner}, beside the pk3's ${wadNum}`);
  if (innerParent !== wadNum) faults.push(`the embedded wad's parent is ${innerParent}, not the pk3's ${wadNum}`);
  return faults;
}

// [rc4l] What is wrong with script 2's log: anything but an out-of-range wad that is neither optional nor authenticated.
export function judgeOutOfRange(log) {
  const m = /optional (-?\d+) auth (-?\d+)/.exec(String(log));
  if (!m) return [`script 2 logged nothing for wad ${BAD_INDEX} (${String(log).trim().slice(0, 120)})`];
  return m[1] === "0" && m[2] === "0" ? [] : [`wad ${BAD_INDEX} reads as optional ${m[1]}, authenticated ${m[2]}, not 0 and 0`];
}

// [rc4l] What is wrong with script 3's log: anything but an empty name for the out-of-range lump.
export function judgeLumpName(log) {
  const m = /lumpname \[(.*?)\]/.exec(String(log));
  if (!m) return [`script 3 logged nothing for lump ${BAD_INDEX} (${String(log).trim().slice(0, 120)})`];
  return m[1] === "" ? [] : [`lump ${BAD_INDEX} is named "${m[1]}", not ""`];
}

// [rc4l] A console command's log, or a fault naming the call when the engine dies on it.
async function logOf(c, command, what) {
  try {
    return { log: await cap.exec(c, command, { quietMs: 300 }) };
  } catch (e) {
    return { died: `the engine died on ${what} (${e.message})` };
  }
}

export async function judgeLaunch(c, inst, label, { wadPath }) {
  await cap.waitTics(c, 10);
  const first = await logOf(c, "puke 1", "script 1");
  const faults = first.died ? [first.died] : judgeWadInfo(first.log, path.basename(wadPath));
  if (!first.died) {
    const second = await logOf(c, "puke 2", `GetWadInfo of wad ${BAD_INDEX}`);
    faults.push(...(second.died ? [second.died] : judgeOutOfRange(second.log)));
    const third = await logOf(c, "puke 3", `LumpGetInfo's name of lump ${BAD_INDEX}`);
    faults.push(...(third.died ? [third.died] : judgeLumpName(third.log)));
  }
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the sprite's namespace, its pk3 and the embedded wad's parent read back, an out-of-range wad is neither optional nor authenticated, and an out-of-range lump has no name`, failed: faults.length ? 1 : 0 };
}

export const SIDES = [{ name: "vk" }];

runScenarioIfMain(import.meta.url, () => ({ name: "acswadinfo", wad: buildAcsWadInfoPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
