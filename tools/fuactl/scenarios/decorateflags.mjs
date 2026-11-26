// [Doruk-Aksoy] Actor flags at UZDoom's bits (features/decorate-uzdoom): exit 0 when every check's flag reads as set at map start or by A_ChangeFlag two tics in,
// on a save an older build wrote and this one loads (FUACTL_OLD_ENGINE or --old PATH), on this build's own save, offline, and on a server, its player, observer and late joiner.
//
// usage:  node scenarios/decorateflags.mjs [PORT] [--old PATH]
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps, pk3 } from "../src/udmf.mjs";
import { acsScripts, callFunc, ACSF, PCD, SCRIPT } from "../src/acs.mjs";
import { runScenarioIfMain, compareNetplay, writeTemplateFile, FREEDOOM2, MUTE } from "../src/template.mjs";
import { withLockedInstance } from "../src/play.mjs";
import { resolveEngine } from "../src/launch.mjs";
import { withNetCopies, addPlayerStarts, netJudge } from "../src/netacs.mjs";
import { SNAP_SERVER_CVARS } from "../src/netplay.mjs";
import * as cap from "../src/capture.mjs";

export const FIRST_TID = 100, CHECK_SCRIPT = 1;
const ROOM = { heightfloor: 0, heightceiling: 128, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 192 };

// [Doruk-Aksoy] Each actor: its flags from DECORATE, those A_ChangeFlag sets (true) or clears (false) two tics in, and the flags it must then read.
export const ACTORS = [
  { cls: "ZxFlags7", flags: ["NODECAL", "FORCEDECAL"], want: { NODECAL: 1, FORCEDECAL: 1 } },
  { cls: "ZxFlags7Set", change: { FORCEDECAL: true }, want: { FORCEDECAL: 1, NODECAL: 0 } },
  { cls: "ZxFlags8", flags: ["FULLVOLSEE", "E1M8BOSS", "E4M6BOSS", "MAP07BOSS2"], want: { FULLVOLSEE: 1, E1M8BOSS: 1, E4M6BOSS: 1, MAP07BOSS2: 1, E2M8BOSS: 0 } },
  { cls: "ZxFlags8Cleared", flags: ["E2M8BOSS", "E3M8BOSS"], change: { E2M8BOSS: false }, want: { E2M8BOSS: 0, E3M8BOSS: 1 } },
  { cls: "ZxRipper", flags: ["NORIPSOUND", "USERIPSTATE", "RIPPERNOPAIN"], want: { NORIPSOUND: 1, USERIPSTATE: 1, RIPPERNOPAIN: 1, RIPEXPLODEONLIMIT: 0 } },
  { cls: "ZxRipperChanged", flags: ["NORIPSOUND"], change: { NORIPSOUND: false, RIPEXPLODEONLIMIT: true }, want: { NORIPSOUND: 0, RIPEXPLODEONLIMIT: 1 } },
  // [Doruk-Aksoy] renderflags and BounceFlags, which a client never heard of a change to before FLAGSET_RENDERFLAGS and FLAGSET_BOUNCEFLAGS; names an older build knows, since it loads this pk3 too.
  { cls: "ZxRenderChanged", change: { INVISIBLE: true }, want: { INVISIBLE: 1 } },
  { cls: "ZxBounceChanged", flags: ["BOUNCEONWALLS"], change: { BOUNCEONWALLS: false, BOUNCEONFLOORS: true }, want: { BOUNCEONWALLS: 0, BOUNCEONFLOORS: 1 } },
  // [Doruk-Aksoy] LIGHTMULTALPHA, in renderflags2 since its old renderflags bit is UZDoom's DONTFLIP; an older build's save has it there.
  { cls: "ZxRender2", flags: ["LIGHTMULTALPHA"], want: { LIGHTMULTALPHA: 1, INVISIBLE: 0 } },
  { cls: "ZxRender2Changed", change: { LIGHTMULTALPHA: true }, want: { LIGHTMULTALPHA: 1 } },
];

// [Doruk-Aksoy] Every [actor index, flag, wanted value], in the order script 1 logs them.
export const CHECKS = ACTORS.flatMap((a, i) => Object.entries(a.want).map(([flag, want]) => ({ actor: i, flag, want })));
export const STRINGS = ["zxflag ", " ", ...new Set(CHECKS.map((c) => c.flag))];

export function buildDecorate() {
  return ACTORS.map((a) => {
    const changes = Object.entries(a.change || {}).map(([flag, on]) => `\t\tTNT1 A 0 A_ChangeFlag("${flag}", ${on ? 1 : 0})`);
    return [`actor ${a.cls}`, "{", "\t+NOGRAVITY", "\t+NOINTERACTION", ...(a.flags || []).map((f) => `\t+${f}`), "\tStates", "\t{", "\tSpawn:",
      "\t\tTNT1 A 2", ...changes, "\t\tTNT1 A -1", "\t\tStop", "\t}", "}"].join("\n") + "\n";
  }).join("");
}

// [Doruk-Aksoy] Script 1 logs "zxflag <check> <CheckFlag>" for each check.
export function checkWords() {
  return [...CHECKS.flatMap((c, i) => [PCD.BEGINPRINT, PCD.PUSHNUMBER, 0, PCD.PRINTSTRING, PCD.PUSHNUMBER, i, PCD.PRINTNUMBER, PCD.PUSHNUMBER, 1, PCD.PRINTSTRING,
    ...callFunc(ACSF.CheckFlag, [FIRST_TID + c.actor, STRINGS.indexOf(c.flag)]), PCD.PRINTNUMBER, PCD.ENDLOG]), PCD.TERMINATE];
}

export function buildMap() {
  const m = new UdmfMap();
  m.room(0, 0, 1024, 512, ROOM, () => ({ side: { texturemiddle: "STARTAN2" } }));
  m.thing({ x: 64, y: 64, angle: 90, type: 1 });
  addPlayerStarts(m, [[128, 64], [192, 64], [256, 64]]);
  ACTORS.forEach((a, i) => m.thing({ x: 128 + 96 * i, y: 384, angle: 0, type: 31500 + i, id: FIRST_TID + i }));
  return m;
}

export function buildDecorateFlagsPk3() {
  const decorate = buildDecorate().replace(/^actor (\w+)$/gm, (line, cls) => `${line} ${31500 + ACTORS.findIndex((a) => a.cls === cls)}`);
  const scripts = withNetCopies([{ code: checkWords(), type: SCRIPT.CLOSED, number: CHECK_SCRIPT }], [CHECK_SCRIPT]);
  return pk3([{ name: "maps/MAP01.wad", data: wad(mapLumps("MAP01", buildMap(), { behavior: acsScripts(scripts, STRINGS) })) }, { name: "decorate.txt", data: decorate }]);
}

// [Doruk-Aksoy] What is wrong with one machine's log of script 1.
export function judgeFlags(log) {
  const got = new Map([...String(log).matchAll(/zxflag (\d+) (-?\d+)/g)].map((m) => [Number(m[1]), Number(m[2])]));
  return CHECKS.flatMap((c, i) => {
    const v = got.get(i);
    const name = `${ACTORS[c.actor].cls} ${c.flag}`;
    if (v === undefined) return [`${name} not logged`];
    return v === c.want ? [] : [`${name} is ${v}, not ${c.want}`];
  });
}

const verdict = (label, faults, pass) => ({ line: faults.length ? `FAIL ${label}: ${faults.join("; ")}` : `PASS ${label}: ${pass}`, failed: faults.length ? 1 : 0 });

// [Doruk-Aksoy] The console steps the offline checks take, a test's fakes in place of an engine's.
export const ENGINE_IO = { exec: cap.exec, waitTics: cap.waitTics, sleep: cap.sleep, waitInLevel: cap.waitInLevel, exists: fs.existsSync };

// [Doruk-Aksoy] Script 1's log on one instance.
async function logFlags(io, c) {
  await io.waitTics(c, 5);
  return io.exec(c, `puke ${CHECK_SCRIPT}`, { quietMs: 400 });
}

// [Doruk-Aksoy] `file` saved and true once it exists.
async function save(io, c, file) {
  await io.exec(c, `save "${file}"`, { quietMs: 600 });
  await io.waitTics(c, 10);
  return io.exists(file);
}

// [Doruk-Aksoy] `file` loaded, until its level runs again.
async function load(io, c, file) {
  await io.exec(c, `load "${file}"`, { quietMs: 600 });
  await io.sleep(1500);
  await io.waitInLevel(c);
  await io.waitTics(c, 10);
}

// [Doruk-Aksoy] The offline checks: the map as started, this build's own save, and with an older build a save it wrote, each loaded here.
export async function compareOffline({ name, wad: data, iwad = FREEDOOM2, port, old = null }, deps = {}) {
  const io = { ...ENGINE_IO, ...deps.io };
  const wadPath = deps.wadPath || writeTemplateFile(name, data);
  const dir = deps.dir || fs.mkdtempSync(path.join(os.tmpdir(), "decorateflags-"));
  const launch = deps.launch || ((engine, body) => withLockedInstance({ port, map: "MAP01", iwad, file: wadPath, engine, extra: { ...MUTE }, width: 1024, height: 640 }, body));
  const engine = deps.engine || resolveEngine();
  const results = [];
  results.push(await launch(engine, async (c) => {
    await io.waitTics(c, 35);
    const started = judgeFlags(await logFlags(io, c));
    const own = path.join(dir, "own.zds");
    const saved = await save(io, c, own);
    if (saved) await load(io, c, own);
    const loaded = saved ? judgeFlags(await logFlags(io, c)) : ["this build wrote no save"];
    return verdict("offline", [...started, ...loaded.map((f) => `own save: ${f}`)], `${CHECKS.length} flags read as set, at map start and from this build's save`);
  }));
  if (!old) {
    results.push({ line: "SKIP older save: no FUACTL_OLD_ENGINE or --old build to write one", failed: 0 });
  } else {
    const file = path.join(dir, "old.zds");
    const saved = await launch(old, async (c) => { await io.waitTics(c, 35); return save(io, c, file); });
    results.push(!saved ? verdict("older save", ["the older build wrote no save"], "") : await launch(engine, async (c) => {
      await io.waitTics(c, 5);
      await load(io, c, file);
      return verdict("older save", judgeFlags(await logFlags(io, c)), `${CHECKS.length} flags read as set from ${path.basename(path.dirname(old))}'s save`);
    }));
  }
  return { lines: results.map((r) => r.line), failed: results.reduce((n, r) => n + r.failed, 0) };
}

// [Doruk-Aksoy] The legacy netcode and the snapshot overhaul, whose server cvars are its settings until they become the defaults.
export const CONFIGS = [{ name: "lan", players: 1, observers: 1 }, { name: "snap", players: 1, observers: 1, serverCvars: SNAP_SERVER_CVARS }];

// [Doruk-Aksoy] Every machine reads the flags it joined with, the late joiner too, each by script 1's CLIENTSIDE copy.
export const judgeNetplay = netJudge({ steps: [{ script: CHECK_SCRIPT, tags: ["flags"] }], late: [{ script: CHECK_SCRIPT, tags: ["flags"] }], by: "server",
  judge: (logs) => judgeFlags(logs.flags), passed: `${CHECKS.length} flags read as set on every machine` });

// [Doruk-Aksoy] Offline first, then hosted.
export async function compareOfflineAndOnline(opts, { offline = compareOffline, online = compareNetplay } = {}) {
  const a = await offline(opts);
  const b = await online(opts);
  return { lines: [...a.lines, ...b.lines], failed: a.failed + b.failed };
}
compareOfflineAndOnline.judgedAlone = true;

// [Doruk-Aksoy] The older build: --old PATH, else FUACTL_OLD_ENGINE.
export function oldEngine(argv = process.argv.slice(2), env = process.env) {
  const i = argv.indexOf("--old");
  return i >= 0 && argv[i + 1] ? argv[i + 1] : env.FUACTL_OLD_ENGINE || null;
}

runScenarioIfMain(import.meta.url, () => ({ name: "decorateflags", wad: buildDecorateFlagsPk3(), maps: ["MAP01"], configs: CONFIGS, old: oldEngine(), judgeNetplay }),
  compareOfflineAndOnline);
