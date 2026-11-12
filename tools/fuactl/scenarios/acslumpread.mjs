// [rc4l] ACS's LumpReadArray on bytes past 127 (zandronum@8fb02634ebdb): exit 0 when a byte of 200 reads into a hub array as 200, not sign-extended to -56.
//
// usage:  node scenarios/acslumpread.mjs
import { Buffer } from "node:buffer";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const DATA_PATH = "zxbytes.dat";
// [rc4l] One byte with its top bit set and one without.
export const BYTES = [200, 5];
const LUMP_OPEN_FULLPATH = 1, HANDLE_VAR = 0, HUB_ARRAY = 0;
export const STRINGS = [DATA_PATH, "bytes", " "];

// [rc4l] Script 1 reads the file's bytes into hub array 0 and logs them.
export function buildScript() {
  const element = (i) => [PCD.PUSHNUMBER, 2, PCD.PRINTSTRING, PCD.PUSHNUMBER, i, PCD.PUSHWORLDARRAY, HUB_ARRAY, PCD.PRINTNUMBER];
  return [
    ...callFunc(ACSF.LumpOpen, [0, -1, LUMP_OPEN_FULLPATH]), PCD.ASSIGNMAPVAR, HANDLE_VAR,
    ...callFunc(ACSF.LumpReadHub, [[PCD.PUSHMAPVAR, HANDLE_VAR], 0, HUB_ARRAY, 0, BYTES.length]), PCD.DROP,
    PCD.BEGINPRINT, PCD.PUSHNUMBER, 1, PCD.PRINTSTRING, ...BYTES.flatMap((_, i) => element(i)), PCD.ENDLOG, PCD.TERMINATE,
  ];
}

export function buildAcsLumpReadPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: buildScript(), type: SCRIPT.CLOSED, number: 1 }], STRINGS) })) },
    { name: DATA_PATH, data: Buffer.from(BYTES) },
  ]);
}

// [rc4l] What is wrong with script 1's log against the file's bytes.
export function judgeBytes(log) {
  const m = /bytes((?: -?\d+)+)/.exec(String(log));
  if (!m) return [`script 1 logged no bytes (${String(log).trim().slice(0, 80)})`];
  const got = m[1].trim().split(" ").map(Number);
  return got.join(" ") === BYTES.join(" ") ? [] : [`the bytes read as ${got.join(" ")}, not ${BYTES.join(" ")}`];
}

export const SIDES = [{ name: "vk" }];

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  const faults = judgeBytes(await cap.exec(c, "puke 1", { quietMs: 300 }));
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: LumpReadHub read ${BYTES.join(" ")}`, failed: faults.length ? 1 : 0 };
}

runScenarioIfMain(import.meta.url, () => ({ name: "acslumpread", wad: buildAcsLumpReadPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
