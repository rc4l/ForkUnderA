// [rc4l] ACS's GetWadInfo and LumpGetInfo's namespace and wad (zandronum@57fe0dc1ae24): exit 0 when a script reads a sprite's namespace, the pk3 holding it, that pk3's name, and an embedded wad's parent.
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
export const WAD_INFO = { NAME: 0, PARENTWAD: 3 };
const LUMP_OPEN_FULLPATH = 1;
// [rc4l] ns_sprites in w_wad.h.
export const NS_SPRITES = 1;
export const SPRITE_PATH = "sprites/zxspa0.png", INNER_WAD = "zxinner.wad", INNER_LUMP = "ZXINNER";

export const STRINGS = [SPRITE_PATH, INNER_LUMP, "ns ", " wad ", " name ", " parent ", " innerwad ", " innerparent "];
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

export function buildAcsWadInfoPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: buildScript(), type: SCRIPT.CLOSED }], STRINGS) })) },
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

export async function judgeLaunch(c, inst, label, { wadPath }) {
  await cap.waitTics(c, 10);
  const faults = judgeWadInfo(await cap.exec(c, "puke 1", { quietMs: 300 }), path.basename(wadPath));
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: the sprite's namespace, its pk3 and the embedded wad's parent read back`, failed: faults.length ? 1 : 0 };
}

export const SIDES = [{ name: "vk" }];

runScenarioIfMain(import.meta.url, () => ({ name: "acswadinfo", wad: buildAcsWadInfoPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
