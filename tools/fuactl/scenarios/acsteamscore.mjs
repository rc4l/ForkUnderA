// [Doruk-Aksoy] ZDaemon's SetTeamScore (features/acs-parity), which our function list declared without code: in team deathmatch, CTF and team LMS, exit 0 when
// script 1 sets the blue team's score to 7 and the red's to 3 (1 each), a second 7 changes nothing (0), an invalid team is refused (0), and GetTeamScore
// reads back 7 and 3, the mode's own kind of score: frags, points or wins.
//
// usage:  node scenarios/acsteamscore.mjs
import { UdmfMap, wad, mapLumps } from "../src/udmf.mjs";
import { acsScripts, ACSF, PCD, SCRIPT, callFunc } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

export const STRINGS = ["ts", " ", "tg"];
export const S = Object.fromEntries(STRINGS.map((s, i) => [s, i]));
// [Doruk-Aksoy] Blue is team 0, red team 1; 99 is no team.
export const TEAM = { blue: 0, red: 1, none: 99 };

const set = (team, value) => callFunc(ACSF.SetTeamScore, [team, value]);
const get = (team) => callFunc(ACSF.GetTeamScore, [team]);

// [Doruk-Aksoy] A log of `tag` and each value after a space.
export function logWords(tag, values) {
  return [PCD.BEGINPRINT, PCD.PUSHNUMBER, S[tag], PCD.PRINTSTRING,
    ...values.flatMap((v) => [PCD.PUSHNUMBER, S[" "], PCD.PRINTSTRING, ...v, PCD.PRINTNUMBER]), PCD.ENDLOG, PCD.TERMINATE];
}

// [Doruk-Aksoy] Script 1 sets the scores and reads them back; script 2 only reads, as a client does.
export const SET_SCRIPT = logWords("ts", [set(TEAM.blue, 7), set(TEAM.red, 3), set(TEAM.blue, 7), set(TEAM.none, 5), get(TEAM.blue), get(TEAM.red)]);
export const READ_SCRIPT = logWords("tg", [get(TEAM.blue), get(TEAM.red)]);
export const WANT = { ts: [1, 1, 0, 0, 7, 3], tg: [7, 3] };

// [Doruk-Aksoy] One room with a player start, a deathmatch start and a start for each team, so every team mode can spawn its player.
export function buildMap() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 }, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  m.thing({ x: 256, y: 256, angle: 90, type: 11 });
  m.thing({ x: 128, y: 448, angle: 270, type: 5080 });
  m.thing({ x: 384, y: 448, angle: 270, type: 5081 });
  return m;
}

export function buildTeamScoreWad(scripts = [{ code: SET_SCRIPT, type: SCRIPT.CLOSED, number: 1 }, { code: READ_SCRIPT, type: SCRIPT.CLOSED, number: 2 }], map = buildMap()) {
  return wad(mapLumps("MAP01", map, { behavior: acsScripts(scripts, STRINGS) }));
}

// [Doruk-Aksoy] The values a log printed after `tag`, or null.
export function parseScores(text, tag) {
  const m = new RegExp(`${tag}((?: -?\\d+)+)`).exec(String(text));
  return m ? m[1].trim().split(" ").map(Number) : null;
}

// [Doruk-Aksoy] What is wrong with a log of `tag` against its wanted values.
export function judgeScores(text, tag) {
  const got = parseScores(text, tag);
  if (!got) return [`script ${tag} logged nothing (${String(text).trim().slice(0, 100)})`];
  return got.join(" ") === WANT[tag].join(" ") ? [] : [`${tag} logged ${got.join(" ")}, not ${WANT[tag].join(" ")}`];
}

// [Doruk-Aksoy] Each team mode with its own score's limit off, so 7 ends no game.
export const SIDES = [
  { name: "teamdm", launchCvars: { deathmatch: 1, teamplay: 1, fraglimit: 0 } },
  { name: "ctf", launchCvars: { ctf: 1, pointlimit: 0 } },
  { name: "teamlms", launchCvars: { teamlms: 1, winlimit: 0 } },
];

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  const faults = judgeScores(await cap.exec(c, "puke 1", { quietMs: 300 }), "ts");
  faults.push(...judgeScores(await cap.exec(c, "puke 2", { quietMs: 300 }), "tg"));
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: SetTeamScore set both teams once, refused a repeat and no team, and GetTeamScore read 7 and 3`, failed: faults.length ? 1 : 0 };
}

runScenarioIfMain(import.meta.url, () => ({ name: "acsteamscore", wad: buildTeamScoreWad(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
