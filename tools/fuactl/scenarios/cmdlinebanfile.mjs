// [rc4l] Two ban files set on the command line (zandronum@0d75cf18b6be): exit 0 when +set sv_banfile "zxa.txt;zxb.txt" leaves the cvar naming both, where the semicolon ran zxb.txt as a command of its own.
//
// usage:  node scenarios/cmdlinebanfile.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const BAN_FILES = "zxa.txt;zxb.txt";

export function buildCmdlineBanfilePk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 256, angle: 90, type: 1 });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m)) }]);
}

// [rc4l] What is wrong with the console's answer for sv_banfile.
export function judgeBanfile(answer) {
  const m = /"sv_banfile" is "([^"]*)"/.exec(String(answer));
  if (!m) return [`sv_banfile gave no value (${String(answer).trim().slice(0, 120)})`];
  return m[1] === BAN_FILES ? [] : [`sv_banfile is "${m[1]}", not "${BAN_FILES}"`];
}

export async function judgeLaunch(c, inst, label) {
  const faults = judgeBanfile(await cap.exec(c, "sv_banfile", { quietMs: 300 }));
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: sv_banfile names both files set on the command line`, failed: faults.length ? 1 : 0 };
}

// [rc4l] Launch cvars go on the command line as +set name value, the value one argument.
export const SIDES = [{ name: "vk", launchCvars: { sv_banfile: BAN_FILES } }];

runScenarioIfMain(import.meta.url, () => ({ name: "cmdlinebanfile", wad: buildCmdlineBanfilePk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
