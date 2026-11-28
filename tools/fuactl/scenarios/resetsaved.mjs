// [Doruk-Aksoy] A map reset puts back everything a map thing started with (features/actor-layout keeps it off the actor): a killed statue respawns, and a living one a script changed is restored, each with its UDMF scale, alpha, style, gravity, score, health, user variable, tid, and the living one's special and its args.
//
// usage:  node scenarios/resetsaved.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, callSpecial, checksScript, failedChecks, ACSF, PCD } from "../src/acs.mjs";
import { SPECIAL } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareEachLaunch } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const STATUE = "ZXResetStatue", STATUE_ED = 15095;
// [Doruk-Aksoy] The statue killed before the reset, the one left alive but changed, and the tid the script moves the living one to.
export const KILLED_TID = 21, KEPT_TID = 22, MOVED_TID = 99;
export const COUNTER_CVAR = "zx_resetsaved_wrong";
export const DEATH_CVAR = "zx_resetsaved_death";
// [Doruk-Aksoy] The living statue's map special runs this script with DEATH_ARG, which the script before the reset changes to 1.
export const DEATH_SCRIPT = 3, DEATH_ARG = 7;
// [Doruk-Aksoy] ACS function numbers (p_acs.cpp EACSFunctions) and GetActorProperty's numbers (APROP_).
const ACSF_SetUserVariable = 24, ACSF_GetUserVariable = 25;
export const APROP = { health: 0, alpha: 3, renderStyle: 4, gravity: 15, score: 22, scaleX: 29, scaleY: 30 };
const FIXED = (v) => Math.round(v * 65536);
// [Doruk-Aksoy] What each statue starts with on the map, as UDMF sets it and as GetActorProperty reads it back (ACS's STYLE_Translucent is 64).
export const MAP_VALUES = { scalex: 1.5, scaley: 0.75, alpha: 0.5, renderstyle: "translucent", gravity: 0.25, score: 33, health: 2, user_mark: 5 };
export const WANT = { scaleX: FIXED(1.5), scaleY: FIXED(0.75), alpha: FIXED(0.5), renderStyle: 64, gravity: FIXED(0.25), score: 33, health: 40, userMark: 5 };
// [Doruk-Aksoy] The strings the scripts use: the counter cvars and the user variable's name.
const STRINGS = [COUNTER_CVAR, DEATH_CVAR, "user_mark"];

// [Doruk-Aksoy] A zombieman that never leaves its spawn state and carries one user variable.
export const DECORATE = `actor ${STATUE} : ZombieMan ${STATUE_ED}\n{\n  var int user_mark;\n  states\n  {\n  Spawn:\n    POSS A -1\n    stop\n  }\n}\n`;

export function buildResetSavedMap() {
  const m = new UdmfMap();
  m.room(0, 0, 768, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 384, y: 64, angle: 90, type: 1 });
  m.thing({ x: 192, y: 384, angle: 270, type: STATUE_ED, id: KILLED_TID, ...MAP_VALUES });
  m.thing({ x: 576, y: 384, angle: 270, type: STATUE_ED, id: KEPT_TID, ...MAP_VALUES, special: SPECIAL.ACS_Execute, arg0: DEATH_SCRIPT, arg2: DEATH_ARG });
  return m;
}

// [Doruk-Aksoy] The words that leave a statue's value on the stack: an actor property, its user variable, or how many things hold a tid.
export const READ = {
  prop: ([tid, prop]) => [PCD.PUSHNUMBER, tid, PCD.PUSHNUMBER, prop, PCD.GETACTORPROPERTY],
  user: (tid) => callFunc(ACSF_GetUserVariable, [tid, [PCD.PUSHNUMBER, 2]]),
  count: (tid) => [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, tid, PCD.THINGCOUNT],
};

const statueChecks = (name, tid) => [
  [`the ${name} statue's scale x`, "prop", [tid, APROP.scaleX], WANT.scaleX],
  [`the ${name} statue's scale y`, "prop", [tid, APROP.scaleY], WANT.scaleY],
  [`the ${name} statue's alpha`, "prop", [tid, APROP.alpha], WANT.alpha],
  [`the ${name} statue's render style`, "prop", [tid, APROP.renderStyle], WANT.renderStyle],
  [`the ${name} statue's gravity`, "prop", [tid, APROP.gravity], WANT.gravity],
  [`the ${name} statue's score`, "prop", [tid, APROP.score], WANT.score],
  [`the ${name} statue's health`, "prop", [tid, APROP.health], WANT.health],
  [`the ${name} statue's user_mark`, "user", tid, WANT.userMark],
  [`one actor holds the ${name} statue's tid`, "count", tid, 1],
];
export const CHECKS = [...statueChecks("respawned", KILLED_TID), ...statueChecks("kept", KEPT_TID), ["nothing keeps the tid the script moved the kept statue to", "count", MOVED_TID, 0]];

// [Doruk-Aksoy] Script 1, before the reset: both statues lose every map value, the kept one also its special's argument and its tid, and the other dies.
function changeStatues() {
  const set = (tid, prop, value) => [PCD.PUSHNUMBER, tid, PCD.PUSHNUMBER, prop, PCD.PUSHNUMBER, value, PCD.SETACTORPROPERTY];
  const change = (tid) => [...set(tid, APROP.scaleX, FIXED(0.5)), ...set(tid, APROP.scaleY, FIXED(2)), ...set(tid, APROP.alpha, FIXED(0.2)),
    ...set(tid, APROP.renderStyle, 1), ...set(tid, APROP.gravity, FIXED(1)), ...set(tid, APROP.score, 99),
    ...callFunc(ACSF_SetUserVariable, [tid, [PCD.PUSHNUMBER, 2], 9]), PCD.DROP];
  return [...change(KILLED_TID), ...change(KEPT_TID),
    ...callSpecial(SPECIAL.Thing_SetSpecial, [KEPT_TID, SPECIAL.ACS_Execute, DEATH_SCRIPT, 0, 1]),
    ...callSpecial(SPECIAL.Thing_ChangeTID, [KEPT_TID, MOVED_TID]),
    ...callSpecial(SPECIAL.Thing_Destroy, [KILLED_TID]), PCD.TERMINATE];
}

// [Doruk-Aksoy] Script 2, after the reset: every check, then the kept statue dies so its restored special runs script 3 with its restored argument.
function checkStatues() {
  const words = checksScript(CHECKS, READ);
  return [...words.slice(0, -1), ...callSpecial(SPECIAL.Thing_Destroy, [KEPT_TID]), PCD.TERMINATE];
}

// [Doruk-Aksoy] Script 3: adds its argument to DEATH_CVAR.
const deathScript = [PCD.PUSHNUMBER, 1, PCD.PUSHNUMBER, 1, PCD.GETCVAR, PCD.PUSHSCRIPTVAR, 0, PCD.ADD, PCD.CALLFUNC, 2, ACSF.SetCVar, PCD.DROP, PCD.TERMINATE];

// [Doruk-Aksoy] Script 2 comes first: checksScript resolves its jumps from the lump's first word.
export function resetSavedBehavior() {
  return acsScripts([
    { code: checkStatues(), open: false, number: 2 },
    { code: changeStatues(), open: false, number: 1 },
    { code: deathScript, open: false, number: DEATH_SCRIPT, args: 1 },
  ], STRINGS);
}

export function buildResetSavedPk3() {
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", buildResetSavedMap(), { behavior: resetSavedBehavior() })) },
    { name: "decorate.txt", data: DECORATE },
    { name: "cvarinfo.txt", data: `server int ${COUNTER_CVAR} = 0;\nserver int ${DEATH_CVAR} = 0;\n` },
  ]);
}

const cvarValue = (text, name) => {
  const m = new RegExp(`"${name}" is "(-?\\d+)"`).exec(String(text));
  return m ? Number(m[1]) : null;
};

// [Doruk-Aksoy] The failures read off the two counters: the check script's bits, and the death script's sum, DEATH_ARG when the kept statue's special and argument came back.
export function judgeCounters(wrong, death) {
  const faults = [];
  if (wrong === null || wrong < 1) faults.push("the check script never ran");
  else faults.push(...failedChecks(CHECKS, wrong));
  if (death !== DEATH_ARG) faults.push(`the kept statue's death ran its special with ${death ?? "nothing"}, not ${DEATH_ARG}`);
  return faults;
}

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  await cap.exec(c, "puke 1");
  await cap.waitTics(c, 10);
  await cap.exec(c, "resetmap");
  await cap.waitTics(c, 15);
  await cap.exec(c, "puke 2");
  await cap.waitTics(c, 15);
  const faults = judgeCounters(cvarValue(await cap.exec(c, COUNTER_CVAR, { quietMs: 400 }), COUNTER_CVAR),
    cvarValue(await cap.exec(c, DEATH_CVAR, { quietMs: 400 }), DEATH_CVAR));
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 }
    : { line: `PASS ${label}: both statues came back with every map value, and the kept one's special ran with ${DEATH_ARG}`, failed: 0 };
}

// [Doruk-Aksoy] Survival, a mode whose maps reset, with monsters, which launches leave out, set before the map loads.
export const SIDES = [{ name: "vk", launchCvars: { survival: 1, sv_nomonsters: 0 } }];

runScenarioIfMain(import.meta.url, () => ({ name: "resetsaved", wad: buildResetSavedPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
