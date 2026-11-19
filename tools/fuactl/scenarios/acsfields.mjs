// [Doruk-Aksoy] ACS field access by name (features/acs-fields): exit 0 when fua_GetField reads native fields as GetActorX, GetActorAngle and the map
// give them, arrays by index, a far actor's x truncated as GetActorX truncates it, and fua_SetField writes user_ variables only.
//
// usage:  node scenarios/acsfields.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareEachLaunch, MUTE } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";
import { FN as HANDLES, logged } from "./acshandles.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const FN = { ...HANDLES, GetField: 24030, SetField: 24031, GetUserVariable: 25 };
export const NEAR = 5, FAR = 8, FAR_X = 40000, ARG2 = 7;
export const DECORATE = "Actor ZxFieldBarrel : ExplosiveBarrel 21000\n{\n\tvar int user_count;\n\tvar int user_list[3];\n}\n";
export const STRINGS = ["f1", " ", "health", "x", "angle", "args", "radius", "tid", "nosuch", "user_count", "user_list"];
const S = Object.fromEntries(STRINGS.map((s, i) => [s, i]));
const ONE = 65536;

const fn = (name, ...args) => callFunc(FN[name], args);
const h = [PCD.PUSHSCRIPTVAR, 0];
const far = [PCD.PUSHSCRIPTVAR, 1];

// [Doruk-Aksoy] Each value script 1 logs, with what it must read.
export const CHECKS = [
  { what: "health", words: fn("GetField", h, S.health), want: 20 },
  { what: "x", words: fn("GetField", h, S.x), want: 256 * ONE },
  { what: "GetActorX", words: [PCD.PUSHNUMBER, NEAR, PCD.GETACTORX], want: 256 * ONE },
  { what: "angle", words: fn("GetField", h, S.angle), want: ONE / 4 },
  { what: "GetActorAngle", words: [PCD.PUSHNUMBER, NEAR, PCD.GETACTORANGLE], want: ONE / 4 },
  { what: "args[2]", words: fn("GetField", h, S.args, 2), want: ARG2 },
  { what: "radius", words: fn("GetField", h, S.radius), want: 10 * ONE },
  { what: "tid", words: fn("GetField", h, S.tid), want: NEAR },
  { what: "an unknown field", words: fn("GetField", h, S.nosuch), want: 0 },
  { what: "args[9]", words: fn("GetField", h, S.args, 9), want: 0 },
  { what: "SetField(user_count, 12)", words: fn("SetField", h, S.user_count, 12), want: 1 },
  { what: "user_count", words: fn("GetField", h, S.user_count), want: 12 },
  { what: "GetUserVariable(user_count)", words: fn("GetUserVariable", NEAR, S.user_count), want: 12 },
  { what: "SetField(user_list[1], 5)", words: fn("SetField", h, S.user_list, 5, 1), want: 1 },
  { what: "user_list[1]", words: fn("GetField", h, S.user_list, 1), want: 5 },
  { what: "SetField(user_list[3])", words: fn("SetField", h, S.user_list, 5, 3), want: 0 },
  { what: "SetField(health)", words: fn("SetField", h, S.health, 1), want: 0 },
  { what: "health after", words: fn("GetField", h, S.health), want: 20 },
  { what: "GetField on handle 0", words: fn("GetField", 0, S.health), want: 0 },
  { what: "SetField on handle 0", words: fn("SetField", 0, S.user_count, 1), want: 0 },
  { what: "the far barrel's x", words: fn("GetField", far, S.x), want: (FAR_X * ONE) | 0 },
  { what: "GetActorX of the far barrel", words: [PCD.PUSHNUMBER, FAR, PCD.GETACTORX], want: (FAR_X * ONE) | 0 },
];

export function buildScript() {
  return [...fn("GetActorHandle", NEAR), PCD.ASSIGNSCRIPTVAR, 0, ...fn("GetActorHandle", FAR), PCD.ASSIGNSCRIPTVAR, 1,
    PCD.BEGINPRINT, PCD.PUSHNUMBER, S.f1, PCD.PRINTSTRING,
    ...CHECKS.flatMap((c) => [PCD.PUSHNUMBER, S[" "], PCD.PRINTSTRING, ...c.words, PCD.PRINTNUMBER]), PCD.ENDLOG, PCD.TERMINATE];
}

export function buildAcsFieldsPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.room(FAR_X - 256, 0, FAR_X + 256, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  m.thing({ x: 256, y: 160, angle: 90, type: 21000, id: NEAR, arg2: ARG2 });
  m.thing({ x: FAR_X, y: 256, angle: 0, type: 21000, id: FAR });
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts([{ code: buildScript(), type: SCRIPT.CLOSED, number: 1 }], STRINGS) })) },
    { name: "decorate.txt", data: Buffer.from(DECORATE, "latin1") }]);
}

// [Doruk-Aksoy] What is wrong with script 1's log against each check.
export function judgeFields(log) {
  const got = logged(log, "f1");
  if (!got) return [`script 1 logged nothing (${String(log).trim().slice(0, 100)})`];
  return CHECKS.flatMap((c, i) => (got[i] === c.want ? [] : [`${c.what} read ${got[i]}, not ${c.want}`]));
}

export const SIDES = [{ name: "vk", launchCvars: { ...MUTE } }];

export async function judgeLaunch(c, inst, label) {
  await cap.waitTics(c, 10);
  const faults = judgeFields(await cap.exec(c, "puke 1", { quietMs: 400 }));
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: ${CHECKS.length} reads and writes by field name answer as the existing getters do, user_ variables only taking writes`, failed: faults.length ? 1 : 0 };
}

runScenarioIfMain(import.meta.url, () => ({ name: "acsfields", wad: buildAcsFieldsPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
