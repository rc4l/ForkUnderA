// [Doruk-Aksoy] ACS actor handles across a hub (features/acs-handles): exit 0 when a handle kept from a hub map resolves each time the player comes back, reads 0
// while away and after its actor was removed, the table holds the same counts round after round, and leaving the hub frees the parked slots.
//
// usage:  node scenarios/acshandleshub.mjs
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, PCD, SCRIPT } from "../src/acs.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";
import { runScenarioIfMain, compareEachLaunch, MUTE } from "../src/template.mjs";
import * as cap from "../src/capture.mjs";
import { FN, counts, logged } from "./acshandles.mjs";

const ROOM = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const HUB = ["MAP01", "MAP02", "MAP03"];
export const OUTSIDE = "MAP04";
export const ROUNDS = 8;
export const MAPINFO = `cluster 1\n{\n\thub\n}\n\ncluster 2\n{\n}\n\n${HUB.map((m) => `map ${m} "${m}"\n{\n\tcluster = 1\n}\n`).join("\n")}\nmap ${OUTSIDE} "${OUTSIDE}"\n{\n\tcluster = 2\n}\n`;
export const STRINGS = ["u1", "u2", "u3", " "];
const S = Object.fromEntries(STRINGS.map((s, i) => [s, i]));
const KEPT = 5, REMOVED = 6;

const fn = (name, ...args) => callFunc(FN[name], args);
const global = (n) => [PCD.PUSHGLOBALVAR, n];
const keep = (n, words) => [...words, PCD.ASSIGNGLOBALVAR, n];

function logScript(tag, values, before = []) {
  return [...before, PCD.BEGINPRINT, PCD.PUSHNUMBER, S[tag], PCD.PRINTSTRING,
    ...values.flatMap((v) => [PCD.PUSHNUMBER, S[" "], PCD.PRINTSTRING, ...v, PCD.PRINTNUMBER]), PCD.ENDLOG, PCD.TERMINATE];
}

export const SCRIPTS = {
  // [Doruk-Aksoy] On MAP01's first visit: a handle to keep, and one to a barrel removed before leaving.
  1: logScript("u1", [global(0), fn("HandleValid", global(0)), fn("HandleValid", global(1))], [
    ...keep(0, fn("GetActorHandle", KEPT)), ...keep(1, fn("GetActorHandle", REMOVED)),
    PCD.PUSHNUMBER, REMOVED, PCD.LSPEC1, SPECIAL.Thing_Remove, PCD.DELAYDIRECT, 2]),
  // [Doruk-Aksoy] On any map: the kept and removed handles, and this map's own barrel's handle.
  2: logScript("u2", [fn("HandleValid", global(0)), fn("HandleValid", global(1)), fn("GetActorHandle", KEPT)]),
  // [Doruk-Aksoy] Outside the hub: the kept handle.
  3: logScript("u3", [fn("HandleValid", global(0))]),
};

export function buildAcsHandlesHubPk3() {
  const scripts = Object.entries(SCRIPTS).map(([number, code]) => ({ code, type: SCRIPT.CLOSED, number: Number(number) }));
  const map = () => {
    const m = new UdmfMap();
    m.room(0, 0, 512, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
    m.thing({ x: 256, y: 64, angle: 90, type: 1 });
    m.thing({ x: 256, y: 160, angle: 0, type: THING.ExplosiveBarrel, id: KEPT });
    m.thing({ x: 128, y: 300, angle: 0, type: THING.ExplosiveBarrel, id: REMOVED });
    return m;
  };
  return pk3([...[...HUB, OUTSIDE].map((name) => ({ name: `maps/${name}.wad`, data: wad(mapLumps(name, map(), { behavior: acsScripts(scripts, STRINGS) })) })),
    { name: "mapinfo.txt", data: Buffer.from(MAPINFO, "latin1") }]);
}

// [Doruk-Aksoy] What is wrong with the run: `first` from MAP01's script 1, each round's MAP02 and MAP01 logs and counts, then the outside map's.
export function judgeHub({ first, rounds, outside, outsideCounts }) {
  const faults = [];
  const [kept, keptValid, removedValid] = logged(first, "u1") || [];
  if (!(kept > 0 && keptValid === 1 && removedValid === 0)) faults.push(`MAP01's first visit logged ${kept} ${keptValid} ${removedValid}, not a handle, 1 and 0`);
  rounds.forEach((r, i) => {
    const away = logged(r.away, "u2") || [];
    if (away[0] !== 0 || away[1] !== 0) faults.push(`round ${i + 1}: away on MAP02 the kept and removed handles read ${away[0]} ${away[1]}, not 0 0`);
    const back = logged(r.back, "u2") || [];
    if (back[0] !== 1 || back[1] !== 0 || back[2] !== kept) faults.push(`round ${i + 1}: back on MAP01 they read ${back[0]} ${back[1]} and the barrel's handle is ${back[2]}, not 1 0 ${kept}`);
    const n = counts(r.counts);
    if (!n) faults.push(`round ${i + 1}: fua_handles printed nothing`);
    else if (i > 0 && JSON.stringify(n) !== JSON.stringify(counts(rounds[0].counts))) faults.push(`round ${i + 1}: the table holds ${JSON.stringify(n)}, not round 1's ${JSON.stringify(counts(rounds[0].counts))}`);
  });
  const [after] = logged(outside, "u3") || [];
  if (after !== 0) faults.push(`outside the hub the kept handle reads ${after}, not 0`);
  const n = counts(outsideCounts);
  if (!n || n.parked !== 0) faults.push(`outside the hub the table holds ${JSON.stringify(n)} with slots parked`);
  return faults;
}

export const SIDES = [{ name: "vk", launchCvars: { ...MUTE } }];

export async function judgeLaunch(c, inst, label) {
  const exec = (text) => cap.exec(c, text, { quietMs: 400 });
  const enter = async (map) => {
    await exec(`changemap ${map}`);
    await cap.waitInLevel(c, undefined, { pressThrough: true });
    await cap.waitTics(c, 5);
  };
  await cap.waitTics(c, 10);
  const first = await exec("puke 1");
  await cap.waitTics(c, 5);
  const rounds = [];
  for (let i = 0; i < ROUNDS; i++) {
    await enter("MAP02");
    const away = await exec("puke 2");
    await enter("MAP03");
    await exec("puke 2");
    await enter("MAP01");
    rounds.push({ away, back: await exec("puke 2"), counts: await exec("fua_handles") });
  }
  await enter(OUTSIDE);
  const outside = await exec("puke 3");
  const faults = judgeHub({ first, rounds, outside, outsideCounts: await exec("fua_handles") });
  return { line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: a kept handle came back with MAP01 ${ROUNDS} times with the table steady at ${JSON.stringify(counts(rounds[0].counts))}, a removed one stayed stale, and leaving the hub freed every parked slot`, failed: faults.length ? 1 : 0 };
}

runScenarioIfMain(import.meta.url, () => ({ name: "acshandleshub", wad: buildAcsHandlesHubPk3(), maps: ["MAP01"], sides: SIDES, judgeLaunch }), compareEachLaunch);
