// [Doruk-Aksoy] ACS actor queries (features/acs-queries): exit 0 when blockmap and class queries find what each FQ_ filter keeps, sort and cap
// their results, stop at 16 a script with one warning, refuse another script's or a freed id, and resume a script waiting mid-loop across a savegame.
//
// usage:  node scenarios/acsqueries.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, AcsScript, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareEachLaunch, MUTE } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";
import { FN as HANDLES, logged } from "./acshandles.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const FN = { ...HANDLES, GetField: 24030, QueryCreate: 24040, QueryFree: 24041, QueryClear: 24042, QueryFilter: 24043, QuerySort: 24044,
  FindActors: 24045, FindActorsOfClass: 24046, FoundActor: 24047, FoundCount: 24048 };
export const FQ = { CLASS: 1, NOTCLASS: 2, ALIVE: 3, DEAD: 4, MONSTER: 5, PLAYER: 6, MISSILE: 7, SHOOTABLE: 8, FLAG: 9, TID: 10, FRIENDLY: 11,
  HOSTILE: 12, SIGHT: 13, EXCLUDE: 14, FLAT: 15, EDGES: 16, NONE: 0, NEAREST: 32, FARTHEST: 33 };
const TAGS = ["q1", "c1", "c2", "l1", "h1", "x1", "s1", "s2", "n1"];
// [Doruk-Aksoy] q0 and q2 are netacsqueries' own.
export const STRINGS = [...TAGS, " ", "tid", "ExplosiveBarrel", "Clip", "Ammo", "BulletPuff", "SHOOTABLE", "NOSUCHFLAG", "NoSuchClass", "q0", "q2"];
const S = Object.fromEntries(STRINGS.map((s, i) => [s, i]));
// [Doruk-Aksoy] Things by tid: barrels A-E (E on a platform, D in a closed room), a zombieman, a killed and a live imp, two friends in D's room, a clip.
export const TID = { A: 11, B: 12, C: 13, D: 14, E: 15, ZOMBIE: 21, DEADIMP: 22, FRIEND1: 23, FRIEND2: 24, IMP: 25, CLIP: 26, PUFF: 30 };
// [Doruk-Aksoy] Globals: the player's, E's and the first friend's handles, then each script's query ids and the resume flag.
export const G = { PLAYER: 0, E: 1, FRIEND1: 2, Q: 3, HELD: 4, RESUME: 5, Q2: 7, SAVED: 8, FREED: 9 };
const LSPEC3 = 6, GT = 22;

const fx = (n) => n * 65536;
export const fn = (name, ...args) => callFunc(FN[name], args);
export const global = (n) => [PCD.PUSHGLOBALVAR, n];
export const keep = (n, words) => [...words, PCD.ASSIGNGLOBALVAR, n];
export const run = (...calls) => calls.flatMap((c) => [...c, PCD.DROP]);
const tidOf = (h) => fn("GetField", h, S.tid);
export const find = (q, x, y, z, r) => fn("FindActors", q, fx(x), fx(y), fx(z), fx(r));
const ofClass = (q, cls, ...rest) => fn("FindActorsOfClass", q, S[cls], ...rest);
export const filter = (q, ...args) => fn("QueryFilter", q, ...args);

function logWords(tag, values) {
  return [PCD.BEGINPRINT, PCD.PUSHNUMBER, S[tag], PCD.PRINTSTRING,
    ...values.flatMap((v) => [PCD.PUSHNUMBER, S[" "], PCD.PRINTSTRING, ...v, PCD.PRINTNUMBER]), PCD.ENDLOG];
}
export const logScript = (tag, values, before = [], after = []) => [...before, ...logWords(tag, values), ...after, PCD.TERMINATE];

const q = global(G.Q), q2 = global(G.Q2), BIG = [1024, 512, 0, 4000];
export const ROOM1 = [512, 512, 0, 2000];

export const SCRIPTS = {
  // [Doruk-Aksoy] Handles kept for the filters, and the imp killed so a corpse is on the map.
  9: [...keep(G.PLAYER, fn("GetActorHandle", 0)), ...keep(G.E, fn("GetActorHandle", TID.E)), ...keep(G.FRIEND1, fn("GetActorHandle", TID.FRIEND1)),
    PCD.PUSHNUMBER, TID.DEADIMP, PCD.PUSHNUMBER, 1000, PCD.PUSHNUMBER, 0, LSPEC3, SPECIAL.Thing_Damage, PCD.TERMINATE],
  // [Doruk-Aksoy] Blockmap queries through each filter, sort and cap; the comment on each value is what it should log.
  1: logScript("q1", [
    [...keep(G.Q, fn("QueryCreate")), ...q], // a query id
    find(q, 300, 300, 0, 150), // 2: A and B
    [...run(fn("QuerySort", q, FQ.NEAREST)), ...find(q, 390, 300, 0, 150)], // 2
    tidOf(fn("FoundActor", q, 0)), tidOf(fn("FoundActor", q, 1)), // B then A
    [...run(fn("QuerySort", q, FQ.FARTHEST, 1)), ...find(q, 390, 300, 0, 150)], tidOf(fn("FoundActor", q, 0)), // 1, A
    [...run(fn("QueryClear", q), filter(q, FQ.TID, TID.C)), ...find(q, 500, 300, 0, 512)], tidOf(fn("FoundActor", q, 0)), // 1, C
    [...run(fn("QueryClear", q, FQ.TID), filter(q, FQ.MONSTER)), ...find(q, ...ROOM1)], // 5 monsters
    [...run(filter(q, FQ.ALIVE)), ...find(q, ...ROOM1)], // 4 alive
    [...run(fn("QueryClear", q, FQ.ALIVE), filter(q, FQ.DEAD)), ...find(q, ...ROOM1)], tidOf(fn("FoundActor", q, 0)), // 1, the dead imp
    [...run(fn("QueryClear", q), filter(q, FQ.PLAYER)), ...find(q, ...ROOM1)], [...fn("FoundActor", q, 0), ...global(G.PLAYER), PCD.EQ], // 1, 1
    [...run(fn("QueryClear", q), filter(q, FQ.CLASS, S.ExplosiveBarrel)), ...find(q, ...BIG)], // 5 barrels
    [...run(filter(q, FQ.EXCLUDE, global(G.E))), ...find(q, ...BIG)], // 4 without E
    [...run(filter(q, FQ.SIGHT, global(G.PLAYER))), ...find(q, ...BIG)], // 3 the player sees
    [...run(fn("QueryClear", q)), ...find(q, 335, 300, 0, 30)], [...run(filter(q, FQ.EDGES)), ...find(q, 335, 300, 0, 30)], // 0, then 1 by A's edge
    [...run(fn("QueryClear", q)), ...find(q, 850, 650, 0, 50)], [...run(filter(q, FQ.FLAT)), ...find(q, 850, 650, 0, 50)], // 0, then 1 ignoring height
    [...run(fn("QueryClear", q), filter(q, FQ.MONSTER), filter(q, FQ.FLAG, S.SHOOTABLE, 0)), ...find(q, ...ROOM1)], // 1 corpse
    [...run(fn("QueryClear", q)), ...filter(q, FQ.FLAG, S.NOSUCHFLAG)], find(q, ...ROOM1), // 0, 0
    [...run(fn("QueryClear", q), filter(q, FQ.CLASS, S.ExplosiveBarrel), filter(q, FQ.SHOOTABLE)), ...find(q, ...BIG)], // 5
    [...run(fn("QueryClear", q), filter(q, FQ.HOSTILE, global(G.FRIEND1)), filter(q, FQ.MONSTER), filter(q, FQ.ALIVE)), ...find(q, ...BIG)], // 2
    [...run(fn("QueryClear", q), filter(q, FQ.FRIENDLY, global(G.FRIEND1)), filter(q, FQ.EXCLUDE, global(G.FRIEND1)), filter(q, FQ.MONSTER)), ...find(q, ...BIG)], tidOf(fn("FoundActor", q, 0)), // 1, friend 2, as the player is friendly too
    [...run(fn("QueryClear", q)), ...filter(q, FQ.CLASS, S.NoSuchClass)], find(q, ...BIG), // 0, 0
    [...run(fn("QueryClear", q)), ...filter(q, FQ.SIGHT, 999999)], find(q, ...BIG), // 0, 0
    [...run(fn("QueryClear", q)), ...filter(q, 99)], // 0
    [...run(fn("QueryFree", q)), ...fn("FoundCount", q)], find(q, 300, 300, 0, 150), // 0, 0
  ]),
  // [Doruk-Aksoy] Class queries: carried inventory left out, subclasses, a sort from a centre, a NOBLOCKMAP puff; then a result whose actor went.
  2: logScript("c1", [
    [...keep(G.Q2, fn("QueryCreate")), ...ofClass(q2, "ExplosiveBarrel")], // 5
    ofClass(q2, "Clip"), // 1: the map's, not the player's
    [...run(fn("QuerySort", q2, FQ.NEAREST)), ...ofClass(q2, "ExplosiveBarrel", 1, fx(700), fx(300), 0)], tidOf(fn("FoundActor", q2, 0)), // 5, C
    ofClass(q2, "Ammo", 1), ofClass(q2, "Ammo", 0), // 1, 0
    [...run(callFunc(ACSF.SpawnForced, [S.BulletPuff, fx(600), fx(600), 0, TID.PUFF, 0]), fn("QueryClear", q2), filter(q2, FQ.TID, TID.PUFF)), ...find(q2, 600, 600, 0, 64)], // 0
    ofClass(q2, "BulletPuff"), // 1
    [...run(fn("QueryClear", q2)), ...ofClass(q2, "NoSuchClass")], // 0
    [...run(fn("QuerySort", q2, FQ.NEAREST), ofClass(q2, "ExplosiveBarrel", 1, fx(300), fx(300), 0)), ...tidOf(fn("FoundActor", q2, 0))], // A
  ], [], [PCD.PUSHNUMBER, TID.A, PCD.LSPEC1, SPECIAL.Thing_Remove, PCD.DELAYDIRECT, 2,
    ...logWords("c2", [fn("FoundActor", q2, 0), fn("FoundCount", q2), tidOf(fn("FoundActor", q2, 1))])]), // 0, 5, B
  // [Doruk-Aksoy] Sixteen queries a script, then two refused.
  3: logScript("l1", [
    [PCD.PUSHNUMBER, 0, ...Array.from({ length: 16 }, () => [...fn("QueryCreate"), PCD.PUSHNUMBER, 0, GT, PCD.ADD]).flat()], fn("QueryCreate"), fn("QueryCreate"),
  ]),
  // [Doruk-Aksoy] A query held while another script tries its id.
  4: logScript("h1", [[...keep(G.HELD, fn("QueryCreate")), ...ofClass(global(G.HELD), "ExplosiveBarrel")]], [], [PCD.DELAYDIRECT, 105]),
  5: logScript("x1", [fn("FoundCount", global(G.HELD)), fn("FoundActor", global(G.HELD), 0), find(global(G.HELD), 300, 300, 0, 150)]),
  // [Doruk-Aksoy] Waits for the resume flag holding a sorted query, across a savegame; a freed id is kept to test the id counter.
  7: [PCD.PUSHNUMBER, 1, PCD.ASSIGNGLOBALVAR, G.RESUME, PCD.TERMINATE],
  8: logScript("n1", [[...fn("QueryCreate"), ...global(G.FREED), GT]]),
};

// [Doruk-Aksoy] Script 6 waits in a loop, so it is built with jump labels; it goes first in the lump so its offsets start at 0.
export function waitingScript() {
  const s = new AcsScript();
  s.emit(...keep(G.SAVED, fn("QueryCreate")), ...run(fn("QuerySort", global(G.SAVED), FQ.NEAREST)),
    ...keep(G.FREED, fn("QueryCreate")), ...run(fn("QueryFree", global(G.FREED))),
    ...logWords("s1", [ofClass(global(G.SAVED), "ExplosiveBarrel", 1, fx(700), fx(300), 0), tidOf(fn("FoundActor", global(G.SAVED), 0))]));
  s.label("wait").emit(...global(G.RESUME)).jump(PCD.IFGOTO, "done").emit(PCD.DELAYDIRECT, 1).jump(PCD.GOTO, "wait");
  s.label("done").emit(...logWords("s2", [fn("FoundCount", global(G.SAVED)), tidOf(fn("FoundActor", global(G.SAVED), 0)), tidOf(fn("FoundActor", global(G.SAVED), 3))]), PCD.TERMINATE);
  return s.words(0);
}

// [Doruk-Aksoy] `withScripts` changes the script list and `withMap` the map, as netacsqueries' CLIENTSIDE copies, own step and player starts do.
export function buildAcsQueriesPk3({ withScripts = (s) => s, withMap = () => {} } = {}) {
  const m = new UdmfMap();
  const room = m.room(0, 0, 1024, 1024, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.block(800, 600, 900, 700, { ...ROOM, heightfloor: 200 }, room, () => ({ texturebottom: "STARTAN2" }));
  m.room(1200, 0, 1700, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 256, y: 64, angle: 90, type: 1 });
  const barrel = (id, x, y) => m.thing({ x, y, angle: 0, type: THING.ExplosiveBarrel, id });
  barrel(TID.A, 300, 300); barrel(TID.B, 400, 300); barrel(TID.C, 700, 300); barrel(TID.D, 1400, 300); barrel(TID.E, 850, 650);
  // [Doruk-Aksoy] Monsters face away from the player, deaf, so none wakes.
  m.thing({ x: 350, y: 500, angle: 90, type: THING.Zombieman, id: TID.ZOMBIE, ambush: true });
  m.thing({ x: 500, y: 800, angle: 90, type: THING.DoomImp, id: TID.DEADIMP, ambush: true });
  m.thing({ x: 150, y: 800, angle: 90, type: THING.DoomImp, id: TID.IMP, ambush: true });
  m.thing({ x: 1300, y: 200, angle: 90, type: THING.Zombieman, id: TID.FRIEND1, friend: true });
  m.thing({ x: 1500, y: 200, angle: 90, type: THING.Zombieman, id: TID.FRIEND2, friend: true });
  m.thing({ x: 600, y: 150, angle: 0, type: THING.Clip, id: TID.CLIP });
  withMap(m);
  const scripts = withScripts([{ code: waitingScript(), type: SCRIPT.CLOSED, number: 6 },
    ...Object.entries(SCRIPTS).map(([number, code]) => ({ code, type: SCRIPT.CLOSED, number: Number(number) }))]);
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(scripts, STRINGS) })) }]);
}

export const FULL_WARNING = /fua_QueryCreate: a script already holds 16 queries/g;
export const WANT = {
  q1: [null, 2, 2, TID.B, TID.A, 1, TID.A, 1, TID.C, 5, 4, 1, TID.DEADIMP, 1, 1, 5, 4, 3, 0, 1, 0, 1, 1, 0, 0, 5, 2, 1, TID.FRIEND2, 0, 0, 0, 0, 0, 0, 0],
  c1: [5, 1, 5, TID.C, 1, 0, 0, 1, 0, TID.A],
  c2: [0, 5, TID.B],
  l1: [16, 0, 0],
  h1: [5],
  x1: [0, 0, 0],
  s1: [4, TID.C],
  s2: [4, TID.C, TID.D],
  n1: [1],
};

// [Doruk-Aksoy] What is wrong with each log: the values that differ from `want` (WANT, or a scenario's own tags and answers), by index; null means any positive value and undefined a value not judged.
export function judgeQueries(logs, want = WANT) {
  const faults = [];
  for (const [tag, values] of Object.entries(want)) {
    const got = logged(logs[tag], tag);
    if (!got) { faults.push(`script ${tag} logged nothing (${String(logs[tag]).trim().slice(0, 100)})`); continue; }
    values.forEach((w, i) => {
      if (w !== undefined && (w === null ? !(got[i] > 0) : got[i] !== w)) faults.push(`${tag}[${i}] is ${got[i]}, not ${w === null ? "an id" : w}`);
    });
  }
  const warnings = (String(logs.l1).match(FULL_WARNING) || []).length;
  if ("l1" in want && warnings !== 1) faults.push(`a full script warned ${warnings} times, not once`);
  return faults;
}

// [Doruk-Aksoy] A locked launch comes up without monsters unless asked.
export const SIDES = [{ name: "vk", launchCvars: { ...MUTE, sv_nomonsters: 0 } }];

export async function judgeLaunch(c, inst, label) {
  const exec = (text) => cap.exec(c, text, { quietMs: 400 });
  const enter = async (command) => {
    await exec(command);
    await cap.waitInLevel(c, undefined, { pressThrough: true });
    await cap.waitTics(c, 5);
  };
  await cap.waitTics(c, 10);
  await exec("puke 9");
  await cap.waitTics(c, 5);
  const logs = { q1: await exec("puke 1"), h1: await exec("puke 4"), x1: await exec("puke 5"), l1: await exec("puke 3") };
  const c12 = await exec("puke 2");
  await cap.waitTics(c, 5);
  logs.c1 = c12;
  logs.c2 = c12 + (await exec("echo fuactl"));
  logs.s1 = await exec("puke 6");
  await exec("save fuactl_acsqueries.zds");
  await cap.waitTics(c, 10);
  // [Doruk-Aksoy] A new game zeroes the id counter, so only the savegame's chunk brings it back.
  await enter("map MAP01");
  await enter("load fuactl_acsqueries.zds");
  logs.s2 = await exec("puke 7");
  logs.n1 = await exec("puke 8");
  const faults = judgeQueries(logs);
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: ${WANT.q1.length + WANT.c1.length} queries found what each filter keeps, sorted and capped, stopped at 16 with one warning, refused another script's and a freed id, and resumed across a savegame`, failed: faults.length ? 1 : 0 };
}

runScenarioIfMain(import.meta.url, () => ({ name: "acsqueries", wad: buildAcsQueriesPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
