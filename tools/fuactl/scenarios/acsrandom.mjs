// [Doruk-Aksoy] ACS named random streams (features/acs-random): exit 0 when a seeded stream repeats its sequence, a CLIENTSIDE script's draws leave the
// gameplay stream alone, named draws never move Random()'s sequence (with compat_oldrandom on too), fixed draws stay in their range, a stream resumes
// across a savegame loaded by a freshly started engine, and a demo plays back the very numbers it recorded.
//
// usage:  node scenarios/acsrandom.mjs
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, PCD, SCRIPT, SCRIPTF } from "../src/acs.mjs";
import { runScenarioIfMain, FREEDOOM2, MUTE } from "../src/template.mjs";
import { launchInstance, stopInstance } from "../src/launch.mjs";
import { attach } from "../src/session.mjs";
import { waitForFile } from "../src/fixtures.mjs";
import * as cap from "../src/capture.mjs";

const ROOM = { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };
export const FN = { Random: 24070, FRandom: 24071, SetRandomSeed: 24072 };
const PCD_RANDOM = 57, ONE = 65536, BIG = 1000000;
export const STRINGS = ["loot", "fx", " ", "r1", "r2", "rc", "g", "f", "e", ""];
const S = Object.fromEntries(STRINGS.map((s, i) => [s, i]));

const fn = (name, ...args) => callFunc(FN[name], args);
const draws = (stream, n) => Array.from({ length: n }, () => fn("Random", stream, 1, BIG));
const globals = (n) => Array.from({ length: n }, () => [PCD.PUSHNUMBER, 1, PCD.PUSHNUMBER, BIG, PCD_RANDOM]);
const log = (tag, values, before = []) => [...before, PCD.BEGINPRINT, PCD.PUSHNUMBER, S[tag], PCD.PRINTSTRING,
  ...values.flatMap((v) => [PCD.PUSHNUMBER, S[" "], PCD.PRINTSTRING, ...v, PCD.PRINTNUMBER]), PCD.ENDLOG, PCD.TERMINATE];
const seed = (stream, n) => [...fn("SetRandomSeed", stream, n), PCD.DROP];

// [Doruk-Aksoy] 1 seeds "loot" and logs five draws; 2 logs the next five; 3, CLIENTSIDE, draws twenty of its own from "loot"; 4 logs three Random()s;
// 5 draws twenty from "loot", then logs three Random()s; 6 seeds "f" and logs three fixed draws from 0 to 1.0 and one from 2.0 to 2.0; 7 logs a draw from an
// empty name, which no stream has.
export const SCRIPTS = {
  1: { code: log("r1", draws(S.loot, 5), seed(S.loot, 7)) },
  2: { code: log("r2", draws(S.loot, 5)) },
  3: { code: log("rc", draws(S.loot, 20)), flags: SCRIPTF.CLIENTSIDE },
  4: { code: log("g", globals(3)) },
  5: { code: log("g", globals(3), draws(S.fx, 20).flatMap((d) => [...d, PCD.DROP])) },
  6: { code: log("f", [...Array.from({ length: 3 }, () => fn("FRandom", S.f, 0, ONE)), fn("FRandom", S.f, 2 * ONE, 2 * ONE)], seed(S.f, 3)) },
  7: { code: log("e", [fn("Random", S[""], 1, 6)]) },
};

export function buildAcsRandomPk3() {
  const m = new UdmfMap();
  m.room(0, 0, 256, 256, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 128, y: 64, angle: 90, type: 1 });
  const scripts = Object.entries(SCRIPTS).map(([n, s]) => ({ code: s.code, type: SCRIPT.CLOSED, number: Number(n), flags: s.flags }));
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", m, { behavior: acsScripts(scripts, STRINGS, { enhanced: true }) })) }]);
}

// [Doruk-Aksoy] The numbers a log printed after its tag, or null.
export function logged(text, tag) {
  const m = new RegExp(`(?:^|\\n)${tag}((?: -?\\d+)+)`).exec(String(text));
  return m ? m[1].trim().split(" ").map(Number) : null;
}

// [Doruk-Aksoy] What is wrong with the single launch's logs.
export function judgeSteps(l) {
  const faults = [];
  const same = (a, b, what) => { if (!a || !b || a.join() !== b.join()) faults.push(`${what}: ${a} and ${b}`); };
  same(l.first, l.again, "a stream seeded the same twice gave different numbers");
  same(l.next, l.nextAfterClient, "a CLIENTSIDE script's draws moved the gameplay stream");
  if (l.first && l.next && l.first.join() === l.next.join()) faults.push("a stream gave its first five again instead of going on");
  same(l.global, l.globalAfterNamed, "named draws moved Random()'s sequence");
  same(l.oldGlobal, l.oldGlobalAfterNamed, "named draws moved Random()'s sequence with compat_oldrandom on");
  const f = l.fixed || [];
  if (f.length !== 4 || f.slice(0, 3).some((v) => v < 0 || v > ONE) || f[3] !== 2 * ONE) faults.push(`fixed draws read ${f}, not three in [0, 1.0] and 2.0`);
  if (!l.empty || l.empty[0] !== 0) faults.push(`a draw from an empty name gave ${l.empty}, not 0`);
  return faults;
}

const exec = (c, text) => cap.exec(c, text, { quietMs: 400 });
const enter = async (c, text) => { await exec(c, text); await cap.waitInLevel(c, undefined, { pressThrough: true }); await cap.waitTics(c, 5); };

// [Doruk-Aksoy] One launch: sequences, the clientside split, Random()'s isolation from a fixed rngseed, fixed draws, and a save for the fresh launch.
async function stepsLaunch(c, save) {
  const l = {};
  await cap.waitTics(c, 10);
  l.first = logged(await exec(c, "puke 1"), "r1");
  l.next = logged(await exec(c, "puke 2"), "r2");
  l.again = logged(await exec(c, "puke 1"), "r1");
  await exec(c, "puke 3");
  l.nextAfterClient = logged(await exec(c, "puke 2"), "r2");
  for (const [old, plain, named] of [[0, "global", "globalAfterNamed"], [1, "oldGlobal", "oldGlobalAfterNamed"]]) {
    await exec(c, `compat_oldrandom ${old}`);
    await exec(c, "rngseed set 4242");
    await enter(c, "map MAP01");
    l[plain] = logged(await exec(c, "puke 4"), "g");
    await enter(c, "map MAP01");
    l[named] = logged(await exec(c, "puke 5"), "g");
  }
  await exec(c, "compat_oldrandom 0");
  l.fixed = logged(await exec(c, "puke 6"), "f");
  l.empty = logged(await exec(c, "puke 7"), "e");
  await enter(c, "map MAP01");
  await exec(c, "puke 1");
  await exec(c, `save "${save}"`);
  await cap.waitTics(c, 10);
  l.afterSave = logged(await exec(c, "puke 2"), "r2");
  await enter(c, `load "${save}"`);
  l.afterLoad = logged(await exec(c, "puke 2"), "r2");
  return l;
}

// [Doruk-Aksoy] Every check: one launch, a fresh launch loading its save, and a demo recorded and played back.
export async function compareRandom({ port = 7796, engine } = {}, deps = {}) {
  const { launch = launchInstance, stop = stopInstance, connect = attach } = deps;
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fua-acsrandom-"));
  const file = path.join(dir, "acsrandom.pk3");
  fs.writeFileSync(file, buildAcsRandomPk3());
  const save = path.join(dir, "random.zds").replace(/\\/g, "/"), demo = path.join(dir, "random.lmp").replace(/\\/g, "/");
  const base = { engine, port, iwad: FREEDOOM2, extraArgs: ["-file", file], cvars: { ...MUTE, fua_render_in_background: 1 } };
  const run = async (opts, body) => {
    const inst = await launch({ ...base, ...opts });
    let c = null;
    try { c = await connect(inst); await cap.waitInLevel(c); return await body(c, inst); } finally { if (c) c.close(); await stop(inst); }
  };
  const faults = [];
  const l = await run({ map: "MAP01" }, (c) => stepsLaunch(c, save));
  faults.push(...judgeSteps(l));
  if (!l.afterSave || !l.afterLoad || l.afterSave.join() !== l.afterLoad.join()) faults.push(`a load in the same engine gave ${l.afterLoad}, not ${l.afterSave}`);
  // [Doruk-Aksoy] A fresh engine holds no stream until the save's chunk makes it, so this is the load that needs it.
  const fresh = await run({ map: "MAP01" }, async (c) => { await enter(c, `load "${save}"`); return logged(await exec(c, "puke 2"), "r2"); });
  if (!fresh || !l.afterSave || fresh.join() !== l.afterSave.join()) faults.push(`a freshly started engine loading the save gave ${fresh}, not ${l.afterSave}`);
  // [Doruk-Aksoy] The demo records the pukes, so playback runs the same scripts at the same tics.
  const recorded = await run({ map: "MAP01", record: demo }, async (c) => {
    await cap.waitTics(c, 10);
    const out = (await exec(c, "puke 1")) + (await exec(c, "puke 2")) + (await exec(c, "puke 4"));
    await cap.waitTics(c, 10);
    await exec(c, "stop");
    await waitForFile(demo.replace(/\//g, path.sep), 30000);
    return out;
  });
  const played = await run({ playdemo: demo }, async (c) => {
    const heard = cap.listen(c);
    for (let i = 0; i < 40 && !logged(heard.text(), "g"); i++) await cap.sleep(250);
    heard.off();
    return heard.text();
  });
  for (const tag of ["r1", "r2", "g"]) {
    const a = logged(recorded, tag), b = logged(played, tag);
    if (!a || !b || a.join() !== b.join()) faults.push(`the demo recorded ${tag} ${a} but played back ${b}`);
  }
  return { lines: [faults.length ? `FAIL MAP01: ${faults.join("; ")}` : "PASS MAP01: a seeded stream repeats, clientside draws and named draws move no other sequence, fixed draws stay in range, a fresh engine resumes a saved stream, and a demo plays back its numbers"], failed: faults.length ? 1 : 0 };
}
compareRandom.judgedAlone = true;

runScenarioIfMain(import.meta.url, () => ({ name: "acsrandom" }), compareRandom);
