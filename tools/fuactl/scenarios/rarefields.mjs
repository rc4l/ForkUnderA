// [Doruk-Aksoy] An actor's rare fields (features/actor-layout's block): a script's change to one statue stays that statue's, its siblings, a child class and a later spawn read their class values, through ACS properties, a DECORATE expression and fua_GetField, and all of it survives a savegame.
//
// usage:  node scenarios/rarefields.mjs [port]
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, checksScript, failedChecks, ACSF, PCD } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch, MUTE } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";
import { FN as HANDLES } from "./acshandles.mjs";

const ROOM = { heightfloor: 0, heightceiling: 160, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const COUNTER_CVAR = "zx_rarefields_wrong";
export const PARENT = "ZXRareParent", CHILD = "ZXRareChild", HEIR = "ZXRareHeir";
export const ED = { [PARENT]: 15096, [CHILD]: 15097, [HEIR]: 15098 };
// [Doruk-Aksoy] The statue the script changes, its sibling, the child that sets its own accuracy, the heir that sets nothing, and the one spawned after the change.
export const TID = { changed: 31, sibling: 32, child: 33, heir: 34, spawned: 35 };
// [Doruk-Aksoy] GetActorProperty's numbers (p_acs.cpp APROP_), GetUserVariable, and acs-fields' GetField.
export const APROP = { score: 22, accuracy: 33, stamina: 34 };
const FN = { GetUserVariable: 25, GetField: 24030, GetActorHandle: HANDLES.GetActorHandle };
export const CLASS = { accuracy: 11, stamina: 22, ripperLevel: 3, childAccuracy: 44 };
export const CHANGED = { score: 77, accuracy: 78, stamina: 79 };
// [Doruk-Aksoy] String 0 is the counter checksScript adds to.
export const STRINGS = [COUNTER_CVAR, "user_sum", "accuracy", "RipperLevel", PARENT];
export const S = Object.fromEntries(STRINGS.map((s, i) => [s, i]));
const FIXED = (v) => v * 65536;

// [Doruk-Aksoy] Statues that are not monsters, so offline's sv_nomonsters keeps them, each tic summing accuracy and stamina through a DECORATE expression.
export const DECORATE = `actor ${PARENT} ${ED[PARENT]}\n{\n  +SOLID\n  Radius 16\n  Height 56\n  Accuracy ${CLASS.accuracy}\n  Stamina ${CLASS.stamina}\n  RipperLevel ${CLASS.ripperLevel}\n  var int user_sum;\n  states\n  {\n  Spawn:\n    POSS A 1 A_SetUserVar("user_sum", accuracy + stamina)\n    loop\n  }\n}\n`
  + `actor ${CHILD} : ${PARENT} ${ED[CHILD]}\n{\n  Accuracy ${CLASS.childAccuracy}\n}\n`
  + `actor ${HEIR} : ${PARENT} ${ED[HEIR]}\n{\n}\n`;

export const SPAWN_AT = { x: 640, y: 384 };

// [Doruk-Aksoy] Parent classes whose user variables grow (and may move) their defaults before a child of each is made, parent k holding k % 16 + 1 variables, enough pairs that a child lands where a moved parent was.
export const GROWN = { count: 64, firstEd: 15100, firstTid: 101 };
export const grownName = (k, child = false) => `ZXRareGrown${k}${child ? "Child" : ""}`;
export const GROWN_DECORATE = Array.from({ length: GROWN.count }, (_, i) => {
  const k = i + 1;
  const vars = Array.from({ length: i % 16 + 1 }, (_, v) => `  var int user_v${v};
`).join("");
  return `actor ${grownName(k)} ${GROWN.firstEd + i}
{
  +SOLID
  Accuracy ${CLASS.accuracy}
${vars}  states
  {
  Spawn:
    POSS A -1
    stop
  }
}
`
    + `actor ${grownName(k, true)} : ${grownName(k)}
{
  Accuracy ${CLASS.childAccuracy}
}
`;
}).join("");
export const GROWN_TIDS = Array.from({ length: GROWN.count }, (_, i) => GROWN.firstTid + i);

export function buildRareFieldsMap() {
  const m = new UdmfMap();
  m.room(0, 0, 768, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 384, y: 64, angle: 90, type: 1 });
  m.thing({ x: 128, y: 384, angle: 270, type: ED[PARENT], id: TID.changed });
  m.thing({ x: 256, y: 384, angle: 270, type: ED[PARENT], id: TID.sibling });
  m.thing({ x: 384, y: 384, angle: 270, type: ED[CHILD], id: TID.child });
  m.thing({ x: 512, y: 384, angle: 270, type: ED[HEIR], id: TID.heir });
  GROWN_TIDS.forEach((tid, i) => m.thing({ x: 32 + (i % 16) * 44, y: 140 + Math.floor(i / 16) * 44, angle: 270, type: GROWN.firstEd + i, id: tid }));
  return m;
}

// [Doruk-Aksoy] The words that leave a statue's value on the stack: a property, its user_sum, a field by name, or how many things hold a tid.
export const READ = {
  prop: ([tid, prop]) => [PCD.PUSHNUMBER, tid, PCD.PUSHNUMBER, prop, PCD.GETACTORPROPERTY],
  user: (tid) => callFunc(FN.GetUserVariable, [tid, S.user_sum]),
  field: ([tid, name]) => callFunc(FN.GetField, [callFunc(FN.GetActorHandle, [tid]), name]),
  count: (tid) => [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, tid, PCD.THINGCOUNT],
  accuracySum: (tids) => [PCD.PUSHNUMBER, 0, ...tids.flatMap((tid) => [PCD.PUSHNUMBER, tid, PCD.PUSHNUMBER, APROP.accuracy, PCD.GETACTORPROPERTY, PCD.ADD])],
};

const classChecks = (name, tid, accuracy) => [
  [`the ${name}'s score`, "prop", [tid, APROP.score], 0],
  [`the ${name}'s accuracy`, "prop", [tid, APROP.accuracy], accuracy],
  [`the ${name}'s stamina`, "prop", [tid, APROP.stamina], CLASS.stamina],
  [`the ${name}'s DECORATE sum`, "user", tid, accuracy + CLASS.stamina],
  [`the ${name}'s RipperLevel field`, "field", [tid, S.RipperLevel], CLASS.ripperLevel],
];
export const CHECKS = [
  ["the changed statue's score", "prop", [TID.changed, APROP.score], CHANGED.score],
  ["the changed statue's accuracy", "prop", [TID.changed, APROP.accuracy], CHANGED.accuracy],
  ["the changed statue's stamina", "prop", [TID.changed, APROP.stamina], CHANGED.stamina],
  ["the changed statue's DECORATE sum", "user", TID.changed, CHANGED.accuracy + CHANGED.stamina],
  ["the changed statue's accuracy field", "field", [TID.changed, S.accuracy], CHANGED.accuracy],
  ...classChecks("sibling", TID.sibling, CLASS.accuracy),
  ...classChecks("child", TID.child, CLASS.childAccuracy),
  ...classChecks("heir", TID.heir, CLASS.accuracy),
  ["one actor was spawned", "count", TID.spawned, 1],
  ...classChecks("spawned statue", TID.spawned, CLASS.accuracy).slice(0, 4),
  ["every grown parent's accuracy beside its child's", "accuracySum", GROWN_TIDS, GROWN.count * CLASS.accuracy],
];

// [Doruk-Aksoy] Script 1: one statue's score, accuracy and stamina change, then another of its class is spawned.
function changeStatue() {
  const set = (prop, value) => [PCD.PUSHNUMBER, TID.changed, PCD.PUSHNUMBER, prop, PCD.PUSHNUMBER, value, PCD.SETACTORPROPERTY];
  return [...set(APROP.score, CHANGED.score), ...set(APROP.accuracy, CHANGED.accuracy), ...set(APROP.stamina, CHANGED.stamina),
    ...callFunc(ACSF.SpawnForced, [S[PARENT], FIXED(SPAWN_AT.x), FIXED(SPAWN_AT.y), 0, TID.spawned, 0]), PCD.DROP, PCD.TERMINATE];
}

// [Doruk-Aksoy] Script 2 comes first: checksScript resolves its jumps from the lump's first word.
export function rareFieldsBehavior() {
  return acsScripts([
    { code: checksScript(CHECKS, READ), open: false, number: 2 },
    { code: changeStatue(), open: false, number: 1 },
  ], STRINGS);
}

export function buildRareFieldsPk3() {
  return pk3([
    { name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", buildRareFieldsMap(), { behavior: rareFieldsBehavior() })) },
    { name: "decorate.txt", data: DECORATE + GROWN_DECORATE },
    { name: "cvarinfo.txt", data: `server int ${COUNTER_CVAR} = 0;\n` },
  ]);
}

const cvarValue = (text, name) => {
  const m = new RegExp(`"${name}" is "(-?\\d+)"`).exec(String(text));
  return m ? Number(m[1]) : null;
};

// [Doruk-Aksoy] The failures one check run's counter shows, each named with when it ran.
export function judgeCounter(when, value) {
  if (value === null || value < 1) return [`the ${when} checks never ran`];
  return failedChecks(CHECKS, value).map((what) => `${when}: ${what}`);
}

export async function judgeLaunch(c, inst, label) {
  const exec = (text) => cap.exec(c, text, { quietMs: 400 });
  const counter = async () => cvarValue(await exec(COUNTER_CVAR), COUNTER_CVAR);
  await cap.waitTics(c, 10);
  await exec("puke 1");
  await cap.waitTics(c, 10);
  await exec("puke 2");
  await cap.waitTics(c, 5);
  const faults = judgeCounter("before the save", await counter());
  await exec(`${COUNTER_CVAR} 0`);
  await exec("save fuactl_rarefields.zds");
  await cap.waitTics(c, 10);
  await exec("load fuactl_rarefields.zds");
  await cap.waitInLevel(c);
  await cap.waitTics(c, 5);
  await exec("puke 2");
  await cap.waitTics(c, 5);
  faults.push(...judgeCounter("after the load", await counter()));
  return faults.length ? { line: `FAIL ${label}: ${faults.join("; ")}`, failed: 1 }
    : { line: `PASS ${label}: the changed statue kept its values alone, its class read its own, before and after a savegame`, failed: 0 };
}

export const SIDES = [{ name: "vk", launchCvars: { ...MUTE } }];

runScenarioIfMain(import.meta.url, () => ({ name: "rarefields", wad: buildRareFieldsPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
