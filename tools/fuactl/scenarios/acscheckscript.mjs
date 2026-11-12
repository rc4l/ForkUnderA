// [rc4l] ACS's CheckScript (zandronum@92ebf7779501): exit 0 when a script finds a script that exists by number and by name, and none for a number or a name no script has.
//
// usage:  node scenarios/acscheckscript.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const NAMED = "zxnamed", MISSING_NAME = "zxmissing", NUMBERED = 5, MISSING_NUMBER = 6;
export const STRINGS = [NAMED, MISSING_NAME, "checks", " "];
const S = Object.fromEntries(STRINGS.map((s, i) => [s, i]));

// [rc4l] Each question CheckScript is asked, as `{ what, args }`, and the answer it should give.
export const QUESTIONS = [
  { what: `number ${NUMBERED}`, args: [NUMBERED, 0], want: 1 },
  { what: `number ${NUMBERED} with one argument`, args: [NUMBERED], want: 1 },
  { what: `number ${MISSING_NUMBER}`, args: [MISSING_NUMBER, 0], want: 0 },
  { what: `name ${NAMED}`, args: [S[NAMED], 1], want: 1 },
  { what: `name ${MISSING_NAME}`, args: [S[MISSING_NAME], 1], want: 0 },
];

// [rc4l] Script 1 logs "checks" and each question's answer after a space.
export function buildScript() {
  const ask = (args) => [...args.flatMap((a) => [PCD.PUSHNUMBER, a]), PCD.CALLFUNC, args.length, ACSF.CheckScript, PCD.PRINTNUMBER];
  return [PCD.BEGINPRINT, PCD.PUSHNUMBER, S.checks, PCD.PRINTSTRING,
    ...QUESTIONS.flatMap((q) => [PCD.PUSHNUMBER, S[" "], PCD.PRINTSTRING, ...ask(q.args)]), PCD.ENDLOG, PCD.TERMINATE];
}

export function buildAcsCheckScriptPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  const scripts = [{ code: buildScript(), type: SCRIPT.CLOSED, number: 1 }, { code: [PCD.TERMINATE], type: SCRIPT.CLOSED, number: NUMBERED }, { code: [PCD.TERMINATE], type: SCRIPT.CLOSED, name: NAMED }];
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(scripts, STRINGS) })) }]);
}

// [rc4l] What is wrong with script 1's log against each question's answer.
export function judgeChecks(log) {
  const m = new RegExp(`checks${" (-?\\d+)".repeat(QUESTIONS.length)}`).exec(String(log));
  if (!m) return [`script 1 logged no answers (${String(log).trim().slice(0, 120)})`];
  return QUESTIONS.flatMap((q, i) => (Number(m[i + 1]) === q.want ? [] : [`CheckScript of ${q.what} is ${m[i + 1]}, not ${q.want}`]));
}

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  const faults = judgeChecks(await cap.exec(c, "puke 1", { quietMs: 300 }));
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: CheckScript finds script ${NUMBERED} and ${NAMED}, and neither ${MISSING_NUMBER} nor ${MISSING_NAME}`, failed: faults.length ? 1 : 0 };
}

export const SIDES = [{ name: "vk" }];

runScenarioIfMain(import.meta.url, () => ({ name: "acscheckscript", wad: buildAcsCheckScriptPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
