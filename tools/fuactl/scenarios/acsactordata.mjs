// [Doruk-Aksoy] ACS per-actor key/value store (features/acs-handles): exit 0 when values set on an actor read back by key in any case, fall back to a
// default when unset, take handles as values, stop at 64 keys with one warning, die with their actor, and survive a savegame.
//
// usage:  node scenarios/acsactordata.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareEachLaunch, MUTE } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";
import { FN as HANDLES, logged } from "./acshandles.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const FN = { ...HANDLES, SetActorData: 24032, GetActorData: 24033, HasActorData: 24034, ClearActorData: 24035 };
export const KEPT = 5, FULL = 6, SPAWNED = 7;
export const MAX_KEYS = 64;
const KEYS = Array.from({ length: MAX_KEYS + 1 }, (_, i) => `k${i}`);
export const STRINGS = ["d1", "d2", "d3", " ", "combo", "COMBO", "missing", "friend", "ExplosiveBarrel", ...KEYS];
const S = Object.fromEntries(STRINGS.map((s, i) => [s, i]));

const fn = (name, ...args) => callFunc(FN[name], args);
const global = (n) => [PCD.PUSHGLOBALVAR, n];
const keep = (n, words) => [...words, PCD.ASSIGNGLOBALVAR, n];
// [Doruk-Aksoy] The sum of SetActorData's results over the first 64 keys, on the second barrel.
const fillKeys = [PCD.PUSHNUMBER, 0, ...KEYS.slice(0, MAX_KEYS).flatMap((k) => [...fn("SetActorData", global(1), S[k], 1), PCD.ADD])];

function logScript(tag, values, before = []) {
  return [...before, PCD.BEGINPRINT, PCD.PUSHNUMBER, S[tag], PCD.PRINTSTRING,
    ...values.flatMap((v) => [PCD.PUSHNUMBER, S[" "], PCD.PRINTSTRING, ...v, PCD.PRINTNUMBER]), PCD.ENDLOG, PCD.TERMINATE];
}

export const SCRIPTS = {
  1: logScript("d1", [
    fn("SetActorData", global(0), S.combo, 3), fn("GetActorData", global(0), S.combo), fn("GetActorData", global(0), S.COMBO),
    fn("HasActorData", global(0), S.combo), fn("GetActorData", global(0), S.missing, -9), fn("HasActorData", global(0), S.missing),
    fn("SetActorData", global(0), S.combo, 4), fn("GetActorData", global(0), S.combo),
    fn("SetActorData", global(0), S.friend, global(1)), fn("GetActorData", global(0), S.friend), global(1),
    fn("ClearActorData", global(0), S.friend), fn("HasActorData", global(0), S.friend),
    fillKeys, fn("SetActorData", global(1), S[KEYS[MAX_KEYS]], 1), fn("SetActorData", global(1), S.k0, 9), fn("GetActorData", global(1), S.k0),
    fn("GetActorData", 0, S.combo, 5), fn("SetActorData", 0, S.combo, 1),
  ], [...keep(0, fn("GetActorHandle", KEPT)), ...keep(1, fn("GetActorHandle", FULL))]),
  // [Doruk-Aksoy] The full barrel removed; the next actor asked about takes its slot without its values.
  2: logScript("d2", [fn("GetActorData", global(1), S.k0, -1), global(1), global(2), fn("HasActorData", global(2), S.k0)], [
    PCD.PUSHNUMBER, FULL, PCD.LSPEC1, SPECIAL.Thing_Remove, PCD.DELAYDIRECT, 2,
    ...callFunc(ACSF.SpawnForced, [S.ExplosiveBarrel, 400 * 65536, 300 * 65536, 0, SPAWNED, 0]), PCD.DROP,
    ...keep(2, fn("GetActorHandle", SPAWNED))]),
  // [Doruk-Aksoy] After a savegame round trip.
  3: logScript("d3", [fn("GetActorData", global(0), S.combo), fn("HasActorData", global(0), S.friend), fn("ClearActorData", global(0)), fn("HasActorData", global(0), S.combo)]),
};

export function buildAcsActorDataPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  m.thing({ x: 256, y: 160, angle: 0, type: THING.ExplosiveBarrel, id: KEPT });
  m.thing({ x: 128, y: 300, angle: 0, type: THING.ExplosiveBarrel, id: FULL });
  const scripts = Object.entries(SCRIPTS).map(([number, code]) => ({ code, type: SCRIPT.CLOSED, number: Number(number) }));
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(scripts, STRINGS) })) }]);
}

export const FULL_WARNING = /fua_SetActorData: a ExplosiveBarrel already carries 64 keys, so "k64" was not set\./g;

// [Doruk-Aksoy] What is wrong with each step's log.
export function judgeActorData(steps) {
  const faults = [];
  const step = (tag) => {
    const v = logged(steps[tag], tag);
    if (!v) faults.push(`script ${tag} logged nothing (${String(steps[tag]).trim().slice(0, 100)})`);
    return v || [];
  };
  const want = (ok, what) => { if (!ok) faults.push(what); };
  const [set, got, gotUpper, has, fallback, hasMissing, reset, regot, setFriend, friend, friendHandle, cleared, hasFriend, filled, past, replace, replaced, zeroGet, zeroSet] = step("d1");
  want(set === 1 && got === 3 && gotUpper === 3 && has === 1, `set and read gave ${set} ${got} ${gotUpper} ${has}, not 1 3 3 1`);
  want(fallback === -9 && hasMissing === 0, `an unset key read ${fallback} and has ${hasMissing}, not -9 and 0`);
  want(reset === 1 && regot === 4, `replacing a value gave ${reset} then ${regot}, not 1 and 4`);
  want(setFriend === 1 && friend === friendHandle && friendHandle > 0, `a handle stored as a value read ${friend}, not ${friendHandle}`);
  want(cleared === 0 && hasFriend === 0, `clearing one key left has = ${hasFriend}`);
  want(filled === 64 && past === 0, `64 keys set ${filled} times and the 65th gave ${past}, not 64 and 0`);
  want(replace === 1 && replaced === 9, `a full actor's existing key took ${replace} and read ${replaced}, not 1 and 9`);
  want(zeroGet === 5 && zeroSet === 0, `handle 0 read ${zeroGet} and set ${zeroSet}, not the default 5 and 0`);
  const warnings = (String(steps.d1).match(FULL_WARNING) || []).length;
  want(warnings === 1, `the full actor warned ${warnings} times, not once`);
  const [staleGet, removed, spawned, spawnedHas] = step("d2");
  want(staleGet === -1, `a removed actor's handle read ${staleGet}, not the default -1`);
  want((spawned & 0xFFFFF) === (removed & 0xFFFFF) && spawnedHas === 0, `the actor taking the removed one's slot has its values: ${spawnedHas}`);
  const [loaded, loadedFriend, clearAll, afterClear] = step("d3");
  want(loaded === 4 && loadedFriend === 0, `after a savegame the value read ${loaded} and the cleared key ${loadedFriend}, not 4 and 0`);
  want(clearAll === 0 && afterClear === 0, `clearing every key left combo: ${afterClear}`);
  return faults;
}

export const SIDES = [{ name: "vk", launchCvars: { ...MUTE } }];

export async function judgeLaunch(c, inst, label) {
  const exec = (text) => cap.exec(c, text, { quietMs: 400 });
  await cap.waitTics(c, 10);
  const steps = { d1: await exec("puke 1"), d2: await exec("puke 2") };
  await cap.waitTics(c, 10);
  await exec("save fuactl_acsactordata.zds");
  await cap.waitTics(c, 10);
  await exec("load fuactl_acsactordata.zds");
  await cap.waitInLevel(c);
  await cap.waitTics(c, 5);
  steps.d3 = await exec("puke 3");
  const faults = judgeActorData(steps);
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: values read back by key, take handles, stop at 64 keys with one warning, die with their actor and survive a savegame`, failed: faults.length ? 1 : 0 };
}

runScenarioIfMain(import.meta.url, () => ({ name: "acsactordata", wad: buildAcsActorDataPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
