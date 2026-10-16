// [rc4l] The fixture set every game-thread check plays: ticcmd demos on a generated map holding every tricky ingredient, on stock maps and on a heavy fight, recorded once and traced tic by tic (sim.trace) so a later build or setting can be held to the same game.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps } from "./udmf.mjs";
import { acs0, callSpecial, consoleCommand, gotoWord, hudMessage, PCD, printString } from "./acs.mjs";
import { ENGINE_THING, SPECIAL, THING } from "./doomnums.mjs";
import { launchInstance, resolveEngine, stopInstance } from "./launch.mjs";
import { FREEDOOM2 } from "./template.mjs";
import { fileURLToPath } from "node:url";
import { attach } from "./session.mjs";
import { sleep, waitInLevel } from "./capture.mjs";
import { describeSimTraceDiff, diffSimTraces, parseSimTrace } from "./simtrace.mjs";

const ARENA = { heightfloor: 0, heightceiling: 256, texturefloor: "FLOOR4_8", textureceiling: "CEIL3_5", lightlevel: 160 };
const WALL = { texturemiddle: "STARTAN2" };
const CAMERA_TID = 5;
const QUAKE_TID = 6;
const TAG = { arena: 1, glow: 2, lift: 3, door: 4 };
const ANIMDEFS = "cameratexture FUAFIX 256 128 fit 128 64\n";
// [rc4l] A window without focus pauses a single-player game, and a demo neither records nor plays while paused.
const RUN_UNFOCUSED = { fua_render_in_background: 1 };

// [rc4l] One arena with each thing a tic changes that the renderer, HUD or sound then reads: powerups, a fog and colour change, a HUD message, a print and a script's console command, a quake, a rotating polyobject, a door with its sound sequence, a lift, a scrolling wall, a glowing light, a camera texture, a dynamic light, and monsters to fight with rockets for decals and particles.
export function buildGameThreadFixtureWad() {
  const m = new UdmfMap();
  const arena = m.room(0, 0, 2048, 1024, { ...ARENA, id: TAG.arena }, (edge) => ({
    side: edge === "east" ? { texturemiddle: "FUAFIX" } : WALL,
    line: edge === "north" ? { special: SPECIAL.Scroll_Texture_Left, arg0: 64 } : {},
  }));
  m.block(256, 704, 512, 960, { ...ARENA, lightlevel: 200, id: TAG.glow }, arena);
  m.block(768, 704, 896, 832, { ...ARENA, heightfloor: 64, id: TAG.lift }, arena, () => ({ texturebottom: "STARTAN2" }));
  m.block(1152, 896, 1280, 1024 - 16, { ...ARENA, heightceiling: 0, id: TAG.door }, arena, () => ({ texturetop: "BIGDOOR1" }));
  // [rc4l] The polyobject's template, a 128 by 8 box walked counter-clockwise so every face points out, in a room of its own.
  const tpl = m.room(4096, 0, 4352, 256, ARENA, () => ({ side: WALL }));
  const face = { texturemiddle: "BIGDOOR1" };
  m.wall(4160, 124, 4288, 124, tpl, face, { special: SPECIAL.Polyobj_StartLine, arg0: 1 });
  m.wall(4288, 124, 4288, 132, tpl, face);
  m.wall(4288, 132, 4160, 132, tpl, face);
  m.wall(4160, 132, 4160, 124, tpl, face);
  m.thing({ x: 4224, y: 128, angle: 1, type: ENGINE_THING.PolyAnchor });
  m.thing({ x: 1024, y: 256, angle: 1, type: ENGINE_THING.PolySpawn });

  m.thing({ x: 128, y: 512, angle: 0, type: 1 });
  m.thing({ x: 192, y: 512, type: THING.RocketLauncher });
  m.thing({ x: 224, y: 512, type: THING.RocketBox });
  m.thing({ x: 600, y: 512, type: THING.InvulnerabilitySphere });
  m.thing({ x: 700, y: 448, type: THING.Infrared });
  m.thing({ x: 800, y: 576, type: THING.RadSuit });
  m.thing({ x: 900, y: 512, type: THING.Berserk });
  for (const [x, y] of [[1600, 300], [1700, 500], [1600, 700], [1750, 800]]) m.thing({ x, y, angle: 180, type: THING.DoomImp });
  for (const [x, y] of [[1500, 400], [1500, 600]]) m.thing({ x, y, angle: 180, type: THING.Zombieman });
  m.thing({ x: 1800, y: 200, angle: 180, type: THING.ShotgunGuy });
  m.thing({ x: 1400, y: 512, angle: 180, type: THING.Demon });
  m.thing({ x: 1024, y: 512, height: 128, type: THING.PointLight, arg0: 255, arg1: 128, arg2: 64, arg3: 200 });
  m.thing({ x: 1900, y: 512, height: 160, angle: 180, type: THING.MapSpot, id: CAMERA_TID });
  m.thing({ x: 600, y: 512, type: THING.MapSpot, id: QUAKE_TID });

  const strings = ["FUAFIX", "gamethread fixture", "fixture hud", "echo fixture console"];
  const setup = [PCD.PUSHNUMBER, CAMERA_TID, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 90, PCD.SETCAMERATOTEXTURE,
    ...callSpecial(SPECIAL.Light_Glow, [TAG.glow, 255, 64, 35]), ...callSpecial(SPECIAL.Plat_PerpetualRaise, [TAG.lift, 16, 35])];
  const loop = [...printString(1), ...hudMessage(2, { id: 1, color: 6, x: 0.5, y: 0.2, hold: 1 }), ...consoleCommand(3),
    ...callSpecial(SPECIAL.Sector_SetFade, [TAG.arena, 96, 0, 0]), ...callSpecial(SPECIAL.Radius_Quake, [2, 20, 0, 10, QUAKE_TID]),
    ...callSpecial(SPECIAL.Polyobj_RotateLeft, [1, 32, 64]), ...callSpecial(SPECIAL.Door_Raise, [TAG.door, 32, 35]),
    PCD.DELAYDIRECT, 70,
    ...callSpecial(SPECIAL.Sector_SetFade, [TAG.arena, 0, 0, 0]), ...callSpecial(SPECIAL.Sector_SetColor, [TAG.arena, 255, 200, 200]),
    PCD.DELAYDIRECT, 70,
    ...callSpecial(SPECIAL.Sector_SetColor, [TAG.arena, 255, 255, 255]), ...gotoWord(setup.length)];
  const behavior = acs0([...setup, ...loop], strings);
  return wad([{ name: "ANIMDEFS", data: ANIMDEFS }, ...mapLumps("MAP01", m, { behavior })]);
}

// [rc4l] Each fixture: its wads (the generated map built, the others found by name in the store), its map and how many tics its demo and traces run.
export const FIXTURES = [
  { name: "gtfix", iwad: "freedoom2.wad", build: buildGameThreadFixtureWad, map: "MAP01", tics: 1050 },
  { name: "freedoom2-map01", iwad: "freedoom2.wad", map: "MAP01", tics: 1050 },
  { name: "freedoom2-map04", iwad: "freedoom2.wad", map: "MAP04", tics: 1050 },
  { name: "sunder-map11", iwad: "doom2.wad", pwads: ["sunder_2512.wad"], map: "MAP11", tics: 700 },
];

// [rc4l] Where demos, baselines and the generated wad are kept, outside the repo since a demo plays only against the wads it was recorded on.
export function fixtureDir(home = os.homedir()) {
  return path.join(home, ".forkundera", "fixtures");
}

// [rc4l] The fixtures named, or every one when none is; a name not in the set throws.
export function pickFixtures(names = [], all = FIXTURES) {
  if (!names.length) return all;
  return names.map((n) => {
    const f = all.find((x) => x.name === n);
    if (!f) throw new Error(`no fixture ${n}; the set is ${all.map((x) => x.name).join(", ")}`);
    return f;
  });
}

const REPO = fileURLToPath(new URL("../../../", import.meta.url));

// [rc4l] A fixture's IWAD as a path, so a demo always plays on the IWAD it was recorded on: Freedoom from the repo, any other found by name in the store, beside the engine or in dist-windows.
export function fixtureIwad(f, store, engineDir, exists = fs.existsSync) {
  if (f.iwad === "freedoom2.wad") return FREEDOOM2;
  for (const dir of [store, engineDir, path.join(REPO, "dist-windows")])
    if (dir && exists(path.join(dir, f.iwad))) return path.join(dir, f.iwad);
  throw new Error(`${f.name} needs ${f.iwad}, which is not in ${[store, engineDir, "dist-windows"].filter(Boolean).join(", ")}`);
}

// [rc4l] A fixture's -file list: the generated wad written into dir, or each pwad found by name in store; missing names the ones not there.
export function fixtureFiles(f, dir, store, listing = store && fs.existsSync(store) ? fs.readdirSync(store) : []) {
  if (f.build) {
    const file = path.join(dir, `${f.name}.wad`);
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(file, f.build());
    return { files: [file], missing: [] };
  }
  const byName = new Map(listing.map((x) => [x.toLowerCase(), x]));
  const files = [], missing = [];
  for (const w of f.pwads || []) {
    const hit = byName.get(w.toLowerCase());
    if (hit) files.push(path.join(store, hit)); else missing.push(w);
  }
  return { files, missing };
}

// [rc4l] The scripted fight a recording plays: moves, turns and fire held for stretches of level time, with god mode and every weapon so the player lives and fires rockets.
export const FIGHT = [
  { at: 0, axis: { forward: 0, side: 0, yaw: 0 }, attack: true },
  { at: 70, axis: { forward: 0.6, side: 0, yaw: 0.25 }, attack: true },
  { at: 175, axis: { forward: 0, side: 0.7, yaw: 0 }, attack: false },
  { at: 280, axis: { forward: 0.5, side: 0, yaw: -0.35 }, attack: true },
  { at: 420, axis: { forward: -0.4, side: -0.5, yaw: 0.2 }, attack: true },
  { at: 560, axis: { forward: 0.7, side: 0, yaw: 0 }, attack: false },
  { at: 700, axis: { forward: 0, side: 0.6, yaw: 0.5 }, attack: true },
  { at: 840, axis: { forward: 0.6, side: -0.3, yaw: -0.2 }, attack: true },
];

// [rc4l] The step of a schedule due at elapsed level time, or null before the first.
export function stepAt(schedule, elapsed) {
  let due = null;
  for (const s of schedule) if (s.at <= elapsed) due = s;
  return due;
}

async function exec(c, text) {
  await c.rpc("console.exec", { text });
}

// [rc4l] Plays the fight on a recording instance until tics of level time have passed.
async function driveFight(c, tics) {
  await exec(c, "god");
  await exec(c, "give all");
  const start = (await c.rpc("sim.tic")).leveltime;
  let applied = null;
  try {
    for (;;) {
      const t = await c.rpc("sim.tic");
      const elapsed = t.leveltime - start;
      if (elapsed >= tics) break;
      const step = stepAt(FIGHT, elapsed);
      if (step && step !== applied) {
        await c.rpc("input.axis", step.axis);
        await exec(c, step.attack ? "+attack" : "-attack");
        applied = step;
      }
      await sleep(50);
    }
  } finally {
    await c.rpc("input.axis", { clear: true }).catch(() => {});
    await exec(c, "-attack").catch(() => {});
  }
}

// [rc4l] Waits for a file a run writes when it finishes, until it has stopped growing, or throws after timeoutMs.
async function waitForFile(file, timeoutMs, proc) {
  const deadline = Date.now() + timeoutMs;
  let last = -1;
  while (Date.now() < deadline) {
    if (fs.existsSync(file)) {
      const size = fs.statSync(file).size;
      if (size > 0 && size === last) return;
      last = size;
    }
    if (proc && proc.exitCode != null) throw new Error(`the engine exited before writing ${file}`);
    await sleep(250);
  }
  throw new Error(`${file} was not written within ${Math.round(timeoutMs / 1000)} s`);
}

// [rc4l] Records a fixture's demo: a new game on its map, the fight, then the recording stopped and written.
export async function recordFixture(f, { engine, store, dir = fixtureDir(), log = () => {} } = {}) {
  const { files, missing } = fixtureFiles(f, dir, store);
  if (missing.length) throw new Error(`${f.name} needs ${missing.join(", ")}, not in ${store}`);
  const demo = path.join(dir, `${f.name}.lmp`);
  fs.rmSync(demo, { force: true });
  const iwad = fixtureIwad(f, store, path.dirname(engine || resolveEngine()));
  const inst = await launchInstance({ engine, iwad, map: f.map, engineDefault: true, cvars: RUN_UNFOCUSED,
    extraArgs: files.length ? ["-file", ...files] : [], record: demo.replace(/\\/g, "/") });
  let c = null;
  try {
    c = await attach(inst);
    await waitInLevel(c, 60000);
    log(`${f.name}: recording ${f.tics} tics`);
    await driveFight(c, f.tics);
    await exec(c, "stop");
    await waitForFile(demo, 30000, inst.proc);
    return demo;
  } finally {
    if (c) c.close();
    await stopInstance(inst);
  }
}

// [rc4l] Plays a fixture's demo with the trace armed from its first tic and returns the trace file.
export async function traceFixture(f, out, { engine, store, dir = fixtureDir(), cvars = {}, log = () => {} } = {}) {
  const demo = path.join(dir, `${f.name}.lmp`);
  if (!fs.existsSync(demo)) throw new Error(`${f.name} has no demo yet: fuactl fixture record ${f.name}`);
  const { files, missing } = fixtureFiles(f, dir, store);
  if (missing.length) throw new Error(`${f.name} needs ${missing.join(", ")}, not in ${store}`);
  fs.rmSync(out, { force: true });
  const iwad = fixtureIwad(f, store, path.dirname(engine || resolveEngine()));
  const inst = await launchInstance({ engine, iwad, engineDefault: true, cvars: { ...RUN_UNFOCUSED, ...cvars }, extraArgs: files.length ? ["-file", ...files] : [],
    playdemo: demo.replace(/\\/g, "/"), postMap: [`fua_simtrace ${f.tics} "${out.replace(/\\/g, "/")}"`] });
  try {
    log(`${f.name}: tracing ${f.tics} tics of its demo`);
    await waitForFile(out, 60000 + f.tics * 150, inst.proc);
    return out;
  } finally {
    await stopInstance(inst);
  }
}

// [rc4l] Traces each fixture and holds it to its saved baseline, saving the baseline when there is none or update asks; returns one result per fixture.
export async function checkFixtures(list, { engine, store, dir = fixtureDir(), cvars = {}, update = false, log = () => {} } = {}) {
  const results = [];
  for (const f of list) {
    const out = path.join(os.tmpdir(), `fuactl-fixture-${f.name}-${process.pid}.trace`);
    await traceFixture(f, out, { engine, store, dir, cvars, log });
    const baseline = path.join(dir, `${f.name}.trace`);
    if (update || !fs.existsSync(baseline)) {
      fs.copyFileSync(out, baseline);
      results.push({ name: f.name, saved: true, lines: [`saved the baseline ${baseline}`] });
      continue;
    }
    const d = diffSimTraces(parseSimTrace(fs.readFileSync(baseline, "utf8")), parseSimTrace(fs.readFileSync(out, "utf8")));
    results.push({ name: f.name, same: d.same, lines: describeSimTraceDiff(d), trace: out });
  }
  return results;
}
