// [Doruk-Aksoy] ACS actor handles (features/acs-handles), single player: exit 0 when a handle names its actor until it is removed, a removed actor's slot comes back
// under the next generation, handles survive a savegame, a map change leaves carried handles stale, and a new game starts the table over.
//
// usage:  node scenarios/acshandles.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareEachLaunch, MUTE } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const FN = { GetActorHandle: 24020, HandleValid: 24021, HandlePointer: 24022, SetActivatorHandle: 24023, SetPointer: 38 };
const AAPTR_TARGET = 2, APROP_HEALTH = 0;
export const BARREL = { kept: 5, removed: 6, spawned: 7 };
export const STRINGS = ["h1", "h2", "h3", "h4", "h5", " ", "ExplosiveBarrel"];
const S = Object.fromEntries(STRINGS.map((s, i) => [s, i]));
const SLOT_MASK = (1 << 20) - 1;
export const slotOf = (h) => h & SLOT_MASK;
export const generationOf = (h) => h >>> 20;

const fn = (name, ...args) => callFunc(FN[name], args);
const global = (n) => [PCD.PUSHGLOBALVAR, n];
const keep = (n, words) => [...words, PCD.ASSIGNGLOBALVAR, n];
const prop = (tid, p) => [PCD.PUSHNUMBER, tid, PCD.PUSHNUMBER, p, PCD.GETACTORPROPERTY];
const isBarrel = callFunc(ACSF.CheckActorClass, [0, S.ExplosiveBarrel]);

// [Doruk-Aksoy] A script logging `tag` and each value after a space, after running `before`.
function logScript(tag, values, before = []) {
  return [...before, PCD.BEGINPRINT, PCD.PUSHNUMBER, S[tag], PCD.PRINTSTRING,
    ...values.flatMap((v) => [PCD.PUSHNUMBER, S[" "], PCD.PRINTSTRING, ...v, PCD.PRINTNUMBER]), PCD.ENDLOG, PCD.TERMINATE];
}

// [Doruk-Aksoy] Each script's values, in the order judgeHandles reads them.
export const SCRIPTS = {
  // [Doruk-Aksoy] Getting, checking, following a pointer and taking the activator from a handle.
  1: logScript("h1", [
    global(0), fn("GetActorHandle", BARREL.kept), fn("HandleValid", global(0)), fn("GetActorHandle", 999), fn("HandleValid", 0), fn("HandleValid", -5),
    callFunc(FN.SetPointer, [AAPTR_TARGET, BARREL.kept]), fn("GetActorHandle", 0, AAPTR_TARGET), fn("HandlePointer", fn("GetActorHandle", 0), AAPTR_TARGET),
    fn("GetActorHandle", 0), fn("SetActivatorHandle", global(0)), prop(0, APROP_HEALTH), isBarrel, fn("SetActivatorHandle", 0), isBarrel,
  ], keep(0, fn("GetActorHandle", BARREL.kept))),
  // [Doruk-Aksoy] A removed actor's handle goes stale, and the next actor asked about takes its slot under the next generation.
  2: logScript("h2", [global(1), fn("HandleValid", global(1)), global(2), fn("HandleValid", global(2))], [
    ...keep(1, fn("GetActorHandle", BARREL.removed)), PCD.PUSHNUMBER, BARREL.removed, PCD.LSPEC1, SPECIAL.Thing_Remove, PCD.DELAYDIRECT, 2,
    ...callFunc(ACSF.SpawnForced, [S.ExplosiveBarrel, 400 * 65536, 300 * 65536, 0, BARREL.spawned, 0]), PCD.DROP,
    ...keep(2, fn("GetActorHandle", BARREL.spawned))]),
  // [Doruk-Aksoy] After a savegame round trip.
  3: logScript("h3", [fn("HandleValid", global(0)), fn("GetActorHandle", BARREL.kept), fn("HandleValid", global(1)), fn("HandleValid", global(2)),
    fn("SetActivatorHandle", global(0)), isBarrel]),
  // [Doruk-Aksoy] On MAP02 after changemap, which keeps global variables.
  4: logScript("h4", [fn("HandleValid", global(0)), fn("HandleValid", global(2)), fn("GetActorHandle", BARREL.kept), global(0)]),
  // [Doruk-Aksoy] After `map MAP01`, a new game.
  5: logScript("h5", [global(0), fn("GetActorHandle", BARREL.kept)]),
};

export function buildAcsHandlesPk3() {
  const scripts = Object.entries(SCRIPTS).map(([number, code]) => ({ code, type: SCRIPT.CLOSED, number: Number(number) }));
  const map = () => {
    const m = new UdmfMap();
    m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
    m.thing({ x: 256, y: 64, angle: 90, type: 1 });
    m.thing({ x: 256, y: 160, angle: 0, type: THING.ExplosiveBarrel, id: BARREL.kept });
    m.thing({ x: 128, y: 300, angle: 0, type: THING.ExplosiveBarrel, id: BARREL.removed });
    return m;
  };
  return pk3(["MAP01", "MAP02"].map((name) => ({ name: `maps/${name}.wad`, data: wad(mapLumps(name, map(), { behavior: acsScripts(scripts, STRINGS) })) })));
}

// [Doruk-Aksoy] The numbers a script logged after its tag, or null.
export function logged(text, tag) {
  const m = new RegExp(`${tag}((?: -?\\d+)+)`).exec(String(text));
  return m ? m[1].trim().split(" ").map(Number) : null;
}

// [Doruk-Aksoy] The table's counts from fua_handles, or null.
export function counts(text) {
  const m = /fua_handles: live (\d+) parked (\d+) free (\d+) retired (\d+)/.exec(String(text));
  return m ? { live: +m[1], parked: +m[2], free: +m[3], retired: +m[4] } : null;
}

// [Doruk-Aksoy] What is wrong with each step's log.
export function judgeHandles(steps) {
  const faults = [];
  const step = (tag) => {
    const v = logged(steps[tag], tag);
    if (!v) faults.push(`script ${tag} logged nothing (${String(steps[tag]).trim().slice(0, 100)})`);
    return v || [];
  };
  const want = (ok, what) => { if (!ok) faults.push(what); };
  const [h, again, valid, missing, zero, negative, pointerSet, viaPointer, viaHandlePointer, player, took, health, barrel, stale, kept] = step("h1");
  want(h > 0, `fua_GetActorHandle gave ${h}, not a handle`);
  want(again === h, `asking again gave ${again}, not the same handle ${h}`);
  want(valid === 1 && missing === 0 && zero === 0 && negative === 0, `validity of the handle, a missing tid, 0 and -5 read ${valid} ${missing} ${zero} ${negative}, not 1 0 0 0`);
  want(pointerSet === 1 && viaPointer === h && viaHandlePointer === h, `following AAPTR_TARGET gave ${viaPointer} and ${viaHandlePointer}, not ${h}`);
  want(player > 0 && player !== h, `the player's handle is ${player}`);
  want(took === 1 && health === 20 && barrel === 1, `fua_SetActivatorHandle gave ${took}, and the activator read health ${health}, barrel ${barrel}`);
  want(stale === 0 && kept === 1, `fua_SetActivatorHandle(0) gave ${stale} and the activator was ${kept ? "" : "not "}kept`);
  const [removed, removedValid, spawned, spawnedValid] = step("h2");
  want(removedValid === 0, "a removed actor's handle still resolves");
  want(spawnedValid === 1 && slotOf(spawned) === slotOf(removed) && generationOf(spawned) === generationOf(removed) + 1,
    `the next actor took slot ${slotOf(spawned)} generation ${generationOf(spawned)}, not slot ${slotOf(removed)} generation ${generationOf(removed) + 1}`);
  const live = counts(steps.counts2);
  want(live && live.live === 3 && live.free === 0, `after the respawn the table held ${JSON.stringify(live)}, not 3 live and 0 free`);
  const [loadedValid, loadedAgain, loadedRemoved, loadedSpawned, loadedTook, loadedBarrel] = step("h3");
  want(loadedValid === 1 && loadedAgain === h && loadedRemoved === 0 && loadedSpawned === 1 && loadedTook === 1 && loadedBarrel === 1,
    `after a savegame round trip: valid ${loadedValid}, again ${loadedAgain} (want ${h}), removed ${loadedRemoved}, spawned ${loadedSpawned}, activator ${loadedTook}/${loadedBarrel}`);
  const [carried, carriedSpawned, map02, carriedValue] = step("h4");
  want(carriedValue === h && carried === 0 && carriedSpawned === 0, `on MAP02 the carried handles read ${carried} and ${carriedSpawned}, not 0`);
  want(map02 > 0 && map02 !== h, `MAP02's barrel got ${map02}`);
  const parked = counts(steps.counts4);
  want(parked && parked.parked === 0, `MAP02's table held ${JSON.stringify(parked)} with slots parked`);
  const [cleared, first] = step("h5");
  want(cleared === 0 && first === 1, `after a new game the global read ${cleared} and the first handle was ${first}, not 0 and 1`);
  return faults;
}

export const SIDES = [{ name: "vk", launchCvars: { ...MUTE } }];

export async function judgeLaunch(c, inst, label) {
  const exec = (text) => cap.exec(c, text, { quietMs: 400 });
  const enter = async (text) => {
    await exec(text);
    await cap.waitInLevel(c, undefined, { pressThrough: true });
    await cap.waitTics(c, 5);
  };
  await cap.waitTics(c, 10);
  const steps = { h1: await exec("puke 1"), h2: await exec("puke 2") };
  await cap.waitTics(c, 10);
  steps.counts2 = await exec("fua_handles");
  await exec("save fuactl_acshandles.zds");
  await cap.waitTics(c, 10);
  await enter("load fuactl_acshandles.zds");
  steps.h3 = await exec("puke 3");
  await enter("changemap MAP02");
  steps.h4 = await exec("puke 4");
  steps.counts4 = await exec("fua_handles");
  await enter("map MAP01");
  steps.h5 = await exec("puke 5");
  const faults = judgeHandles(steps);
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: handles resolve, go stale on removal, reuse a slot one generation on, survive a savegame, read 0 after a map change, and start over in a new game`, failed: faults.length ? 1 : 0 };
}

runScenarioIfMain(import.meta.url, () => ({ name: "acshandles", wad: buildAcsHandlesPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
