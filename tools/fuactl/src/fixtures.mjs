// [rc4l] The fixture set every game-thread check plays: ticcmd demos on a generated map holding every tricky ingredient, on stock maps and on a heavy fight, recorded once and traced tic by tic (sim.trace) so a later build or setting can be held to the same game.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { UdmfMap, wad, mapLumps } from "./udmf.mjs";
import { AcsScript, ACSF, acs0, callFunc, callSpecial, consoleCommand, gotoWord, hudMessage, PCD, printString } from "./acs.mjs";
import { ENGINE_THING, SPECIAL, THING } from "./doomnums.mjs";
import { launchInstance, resolveEngine, stopInstance } from "./launch.mjs";
import { FREEDOOM2 } from "./template.mjs";
import { fileURLToPath } from "node:url";
import { attach } from "./session.mjs";
import { exec as execOut, sleep, waitInLevel } from "./capture.mjs";
import { captureVulkan, standAt } from "./shot.mjs";
import { boxDiff, boxDiffMax, decodePNG } from "./pngcore.mjs";
import { describeSimTraceDiff, diffSimTraces, parseSimTrace } from "./simtrace.mjs";
import { scaled } from "./timeouts.mjs";

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

// [rc4l] gtmod's lumps a mod brings, each one a thing the game thread could reorder: a mod cvar and a KEYCONF alias setting it, a lamp whose frames attach a GLDEFS light, a status bar that interpolates and draws the mugshot, two maps chained by MAPINFO, and a camera texture.
const MOD_LUMPS = {
  MAPINFO: 'map MAP01 "gtmod one"\n{\n\tnext = "MAP02"\n\tnointermission\n\tsky1 = "SKY1", 0\n}\nmap MAP02 "gtmod two"\n{\n\tnext = "MAP01"\n\tsky1 = "SKY1", 0\n}\n',
  CVARINFO: "server int gtm_probe = 0;\n",
  KEYCONF: 'alias gtm_alias "gtm_probe 7"\n',
  DECORATE: 'actor GtmLamp : TechLamp 30001\n{\n\tstates\n\t{\n\tSpawn:\n\t\tTLMP A 5 bright\n\t\tTLMP B 5 bright A_Jump(128, "Spawn")\n\t\tTLMP C 5 bright\n\t\tloop\n\t}\n}\n',
  GLDEFS: "pointlight GTMLIGHT\n{\n\tcolor 1.0 0.4 0.1\n\tsize 48\n}\nobject GtmLamp\n{\n\tframe TLMPA { light GTMLIGHT }\n\tframe TLMPC { light GTMLIGHT }\n}\n",
  SBARINFO: 'base Doom;\nheight 32;\nstatusbar normal\n{\n\tdrawimage "STBAR", 0, 168;\n\tdrawnumber 3, BIGFONT, untranslated, health, interpolate(3), 90, 171;\n\tdrawmugshot "STF", 5, 143, 168;\n}\n',
  ANIMDEFS: "cameratexture GTMCAM 256 128 fit 128 64\n",
};
// [rc4l] The things gtmod's script names by tid.
const MOD_TID = { sky: 20, camera: 21, screen: 22, lamp: 23, quake: 24 };
// [rc4l] The map variables gtmod's script keeps what it read in, which the trace's ACS fingerprint holds.
const MOD_VAR = { tic: 0, set: 1, alias: 2, command: 3, screenWidth: 4, renderCvar: 5, justHit: 6, camera: 7, floorZ: 8, gravity: 9 };

// [rc4l] gtmod's OPEN script: every tic it sets a mod cvar and runs console commands, reading each back in the same tic, reads the screen, a render cvar, the skybox's JUSTHIT, the player's camera, a moving floor and the gravity, and plays a looping sound; at set tics it removes the camera the player looks through, sets a render cvar and the gravity from the console, quakes, sets the mugshot, writes a HUD message in a font no one loaded yet, changes the sky, fades a HUD message, autosaves and exits the level.
export function gtmodScript() {
  const strings = ["gtm_probe", "gtm_alias", "gtm_probe 11", "r_drawfuzz", "JUSTHIT", "GTMCAM", "weapons/pistol", "sv_gravity 400", "gtm hud", "Grin", "sv_gravity", "r_drawfuzz 1",
    "STKEYS0", "a", "SKY3"];
  const s = new AcsScript();
  const read = (cvar, v) => s.emit(PCD.PUSHNUMBER, cvar, PCD.GETCVAR, PCD.ASSIGNMAPVAR, v);
  const at = (tic, words) => {
    const skip = `skip${tic}`;
    s.emit(PCD.PUSHMAPVAR, MOD_VAR.tic, PCD.PUSHNUMBER, tic, PCD.EQ).jump(PCD.IFNOTGOTO, skip).emit(...words).label(skip);
  };
  s.emit(PCD.PUSHNUMBER, MOD_TID.screen, PCD.PUSHNUMBER, 5, PCD.PUSHNUMBER, 90, PCD.SETCAMERATOTEXTURE,
    ...callSpecial(SPECIAL.Plat_PerpetualRaise, [TAG.lift, 16, 35]), ...callSpecial(SPECIAL.ChangeCamera, [MOD_TID.camera, 1, 0]));
  s.label("loop").emit(PCD.INCMAPVAR, MOD_VAR.tic);
  s.emit(PCD.PUSHNUMBER, 0, PCD.PUSHMAPVAR, MOD_VAR.tic, PCD.CALLFUNC, 2, ACSF.SetCVar, PCD.DROP);
  read(0, MOD_VAR.set);
  s.emit(...consoleCommand(1));
  read(0, MOD_VAR.alias);
  s.emit(...consoleCommand(2));
  read(0, MOD_VAR.command);
  s.emit(PCD.GETSCREENWIDTH, PCD.ASSIGNMAPVAR, MOD_VAR.screenWidth);
  read(3, MOD_VAR.renderCvar);
  s.emit(...callFunc(ACSF.CheckFlag, [MOD_TID.sky, 4]), PCD.ASSIGNMAPVAR, MOD_VAR.justHit);
  s.emit(PCD.PUSHNUMBER, 0, PCD.CHECKPLAYERCAMERA, PCD.ASSIGNMAPVAR, MOD_VAR.camera);
  s.emit(PCD.PUSHNUMBER, TAG.lift, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 0, PCD.GETSECTORFLOORZ, PCD.ASSIGNMAPVAR, MOD_VAR.floorZ);
  read(10, MOD_VAR.gravity);
  s.emit(...callFunc(ACSF.PlaySound, [MOD_TID.lamp, 6, 2, 65536, 1]), PCD.DROP);
  at(100, callSpecial(SPECIAL.Thing_Remove, [MOD_TID.camera]));
  at(150, consoleCommand(11));
  at(200, consoleCommand(7));
  at(250, [...callSpecial(SPECIAL.Radius_Quake, [3, 30, 0, 12, MOD_TID.quake]), PCD.PUSHNUMBER, 9, PCD.SETMUGSHOTSTATE]);
  // [rc4l] A font loaded from a tic reads its lump and adds a font and textures a frame draws with, and a new sky sets the renderer's sky scale, both the main thread's work.
  at(270, [PCD.PUSHNUMBER, 12, PCD.SETFONT, ...hudMessage(13, { id: 3, x: 0.2, y: 0.7, hold: 2 })]);
  at(290, [PCD.PUSHNUMBER, 14, PCD.PUSHNUMBER, 14, PCD.CHANGESKY]);
  at(300, hudMessage(8, { type: 1, id: 2, color: 4, x: 0.5, y: 0.3, hold: 1, fade: 2 }));
  at(400, callSpecial(SPECIAL.Autosave, [0]));
  at(560, callSpecial(SPECIAL.Exit_Normal, [0]));
  s.emit(PCD.DELAYDIRECT, 1).jump(PCD.GOTO, "loop");
  return acs0(s.words(), strings);
}

// [rc4l] gtmod: a mod's content in one arena, its sky drawn through a skybox, a lamp, a moving floor, a camera texture, a camera the script removes, monsters, and a second map the script exits to.
export function buildGameThreadModWad() {
  const m = new UdmfMap();
  const arena = m.room(0, 0, 2048, 1024, { ...ARENA, textureceiling: "F_SKY1", id: TAG.arena }, (edge) => ({
    side: edge === "east" ? { texturemiddle: "GTMCAM" } : WALL }));
  m.block(768, 704, 896, 832, { ...ARENA, textureceiling: "F_SKY1", heightfloor: 64, id: TAG.lift }, arena, () => ({ texturebottom: "STARTAN2" }));
  // [rc4l] The skybox, sealed away, its viewpoint picked for the arena's sky.
  m.room(4096, 0, 4352, 256, { ...ARENA, lightlevel: 255 }, () => ({ side: WALL }));
  m.thing({ x: 4224, y: 128, height: 64, type: THING.SkyViewpoint, id: MOD_TID.sky });
  m.thing({ x: 64, y: 64, type: THING.SkyPicker, arg0: MOD_TID.sky });
  m.thing({ x: 128, y: 512, angle: 0, type: 1 });
  m.thing({ x: 1000, y: 512, type: 30001, id: MOD_TID.lamp });
  m.thing({ x: 600, y: 300, height: 96, angle: 0, type: THING.MapSpot, id: MOD_TID.camera });
  m.thing({ x: 1900, y: 512, height: 160, angle: 180, type: THING.MapSpot, id: MOD_TID.screen });
  m.thing({ x: 700, y: 512, type: THING.MapSpot, id: MOD_TID.quake });
  for (const [x, y] of [[1600, 300], [1700, 700]]) m.thing({ x, y, angle: 180, type: THING.DoomImp });
  for (const [x, y] of [[1500, 500], [1800, 500]]) m.thing({ x, y, angle: 180, type: THING.Zombieman });
  const two = new UdmfMap();
  two.room(0, 0, 1024, 1024, ARENA, () => ({ side: WALL }));
  two.thing({ x: 128, y: 512, angle: 0, type: 1 });
  two.thing({ x: 800, y: 512, angle: 180, type: THING.DoomImp });
  const lumps = Object.entries(MOD_LUMPS).map(([name, data]) => ({ name, data }));
  return wad([...lumps, ...mapLumps("MAP01", m, { behavior: gtmodScript() }), ...mapLumps("MAP02", two)]);
}

// [rc4l] Each fixture: its wads (the generated map built, the others found by name in the store), its map, how many tics its traces run, and how many its demo records when that is more.
export const FIXTURES = [
  { name: "gtfix", iwad: "freedoom2.wad", build: buildGameThreadFixtureWad, map: "MAP01", tics: 1050 },
  { name: "gtmod", iwad: "freedoom2.wad", build: buildGameThreadModWad, map: "MAP01", tics: 700, recordTics: 760 },
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

// [rc4l] Plays the fight on a recording instance until tics of game time have passed, which a level exit does not restart.
async function driveFight(c, tics) {
  await exec(c, "god");
  await exec(c, "give all");
  const start = (await c.rpc("sim.tic")).gametic;
  let applied = null;
  try {
    for (;;) {
      const t = await c.rpc("sim.tic");
      const elapsed = t.gametic - start;
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
  timeoutMs = scaled(timeoutMs);
  const deadline = Date.now() + timeoutMs;
  let last = -1;
  while (Date.now() < deadline) {
    if (fs.existsSync(file)) {
      const size = fs.statSync(file).size;
      if (size > 0 && size === last) return;
      last = size;
    }
    if (proc && (proc.exitCode != null || proc.signalCode)) throw new Error(`the engine ${proc.signalCode ? `was killed by ${proc.signalCode}` : "exited"} before writing ${file}`);
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
    await waitInLevel(c, scaled(60000));
    log(`${f.name}: recording ${f.tics} tics`);
    await driveFight(c, f.recordTics || f.tics);
    await exec(c, "stop");
    await waitForFile(demo, 30000, inst.proc);
    return demo;
  } finally {
    if (c) c.close();
    await stopInstance(inst);
  }
}

// [rc4l] Launches a fixture's demo on the wads it was recorded on, the Vulkan backend alone unless gl, the game running unfocused.
async function launchDemo(f, { engine, store, dir, cvars = {}, gl = false, postMap = [] }) {
  const demo = path.join(dir, `${f.name}.lmp`);
  if (!fs.existsSync(demo)) throw new Error(`${f.name} has no demo yet: fuactl fixture record ${f.name}`);
  const { files, missing } = fixtureFiles(f, dir, store);
  if (missing.length) throw new Error(`${f.name} needs ${missing.join(", ")}, not in ${store}`);
  const iwad = fixtureIwad(f, store, path.dirname(engine || resolveEngine()));
  return launchInstance({ engine, iwad, engineDefault: !gl, cvars: { ...RUN_UNFOCUSED, ...cvars }, extraArgs: files.length ? ["-file", ...files] : [],
    playdemo: demo.replace(/\\/g, "/"), postMap });
}

// [rc4l] Plays a fixture's demo with the trace armed from its first tic and returns the trace file.
export async function traceFixture(f, out, { engine, store, dir = fixtureDir(), cvars = {}, log = () => {} } = {}) {
  fs.rmSync(out, { force: true });
  const inst = await launchDemo(f, { engine, store, dir, cvars, postMap: [`fua_simtrace ${f.tics} "${out.replace(/\\/g, "/")}"`] });
  try {
    log(`${f.name}: tracing ${f.tics} tics of its demo`);
    await waitForFile(out, 60000 + f.tics * 150, inst.proc);
    return out;
  } finally {
    await stopInstance(inst);
  }
}

// [rc4l] A savegame one engine writes and every engine loads: the fixture's map played saveTic tics with nobody pressing anything and saved, then loaded and traced tics tics by each reader, each reader's trace held to the first's; one result per later reader.
export async function saveCompatFixture(f, { writer, readers, names = readers, store, dir = fixtureDir(), saveTic = 300, tics = 300, log = () => {} }) {
  const { files, missing } = fixtureFiles(f, dir, store);
  if (missing.length) throw new Error(`${f.name} needs ${missing.join(", ")}, not in ${store}`);
  const iwad = fixtureIwad(f, store, path.dirname(writer));
  const extraArgs = files.length ? ["-file", ...files] : [];
  const tag = `${f.name}-${process.pid}`;
  const save = path.join(os.tmpdir(), `fuactl-save-${tag}.zds`).replace(/\\/g, "/");
  fs.rmSync(save, { force: true });
  const inst = await launchInstance({ engine: writer, iwad, map: f.map, engineDefault: true, cvars: RUN_UNFOCUSED, extraArgs });
  let c = null;
  try {
    c = await attach(inst);
    await waitInLevel(c, scaled(60000));
    await waitLevelTime(c, saveTic);
    await exec(c, `save "${save}"`);
    await waitForFile(save, 30000, inst.proc);
  } finally {
    if (c) c.close();
    await stopInstance(inst);
  }
  const traces = [];
  for (const engine of readers) {
    const out = path.join(os.tmpdir(), `fuactl-load-${tag}-${traces.length}.trace`);
    fs.rmSync(out, { force: true });
    const r = await launchInstance({ engine, iwad, engineDefault: true, cvars: RUN_UNFOCUSED, extraArgs, loadgame: save,
      postMap: [`fua_simtrace ${tics} "${out.replace(/\\/g, "/")}"`] });
    try {
      log(`${f.name}: ${names[traces.length]} loads the save and traces ${tics} tics`);
      await waitForFile(out, 60000 + tics * 150, r.proc);
      traces.push(parseSimTrace(fs.readFileSync(out, "utf8")));
    } finally {
      await stopInstance(r);
    }
  }
  return traces.slice(1).map((t, i) => {
    const d = diffSimTraces(traces[0], t);
    return { name: `${f.name}: loaded by ${names[i + 1]} against ${names[0]}`, same: d.same, lines: describeSimTraceDiff(d) };
  });
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

// [rc4l] The views each fixture's pictures are taken from at a tic of its demo: the demo's own player, or a camera stood at while paused.
export const PICTURES = {
  gtfix: { tic: 420, views: [{ name: "player" }, { name: "machines", at: { x: 600, y: 300, z: 0, angle: 45, pitch: 0 } },
    { name: "screen", at: { x: 1300, y: 512, z: 0, angle: 0, pitch: 0 } }] },
  gtmod: { tic: 320, views: [{ name: "player" }] },
  "freedoom2-map01": { tic: 420, views: [{ name: "player" }] },
  "freedoom2-map04": { tic: 420, views: [{ name: "player" }] },
  "sunder-map11": { tic: 350, views: [{ name: "player" }] },
};

// [rc4l] A capture held to its baseline PNG by its block differences: saved when there is none or update asks, and differing when its largest block moved more than tolerance.
export function comparePicture(file, baseline, { update = false, tolerance = 2 } = {}) {
  if (update || !fs.existsSync(baseline)) {
    fs.mkdirSync(path.dirname(baseline), { recursive: true });
    fs.copyFileSync(file, baseline);
    return { saved: true, line: `saved the baseline ${baseline}` };
  }
  const a = decodePNG(baseline), b = decodePNG(file);
  if (a.w !== b.w || a.h !== b.h) return { same: false, line: `the frame is ${b.w}x${b.h}, its baseline ${a.w}x${a.h}` };
  const box = [0, 0, 1, 1], mean = boxDiff(a, b, box), max = boxDiffMax(a, b, box);
  const same = max <= tolerance;
  return { same, mean, max, line: `${same ? "matches" : "differs from"} its baseline: largest block ${max.toFixed(2)}, mean ${mean.toFixed(3)} (tolerance ${tolerance})${same ? "" : `, ${file}`}` };
}

// [rc4l] The numbers fua_vis_report gives for walks checked against GL: how many frames each check compared and how many differed.
export function parseVisReport(text) {
  const num = (re) => { const m = re.exec(text); return m ? m.slice(1).map(Number) : null; };
  const walk = num(/visibility: (\d+) frames walked beside GL, (\d+) differed/);
  const sky = num(/sky: (\d+) frames derived beside GL, (\d+) differed/);
  const portal = num(/portal views: (\d+) frames derived beside GL .*?\), (\d+) differed/);
  return { walked: walk ? walk[0] : 0, differed: (walk ? walk[1] : 0) + (sky ? sky[1] : 0) + (portal ? portal[1] : 0), found: !!walk };
}

// [rc4l] Plays a fixture's demo with the frame clock pinned, so animations are at the same moment in every run, and attaches once its level is up.
async function playTo(f, { engine, store, dir, cvars, gl = false }) {
  // [rc4l] No console notices either, since a capture's own "wrote <file>" would land in the next one with its path and process id.
  const inst = await launchDemo(f, { engine, store, dir, cvars: { fua_debug_frameclock: 1000, con_notifylines: 0, ...cvars }, gl });
  const c = await attach(inst);
  await waitInLevel(c, scaled(60000));
  return { inst, c };
}

async function waitLevelTime(c, tic, { paused = false, timeoutMs = scaled(120000) } = {}) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const t = await c.rpc("sim.tic");
    if (t.leveltime >= tic && (!paused || t.paused)) return t;
    await sleep(100);
  }
  throw new Error(`the demo never reached tic ${tic}`);
}

// [rc4l] A fixture's views captured at its picture tic and held to their baselines under dir/pictures; one result per view.
export async function pictureFixture(f, { engine, store, dir = fixtureDir(), cvars = {}, update = false, tolerance = 2, log = () => {} } = {}) {
  const spec = PICTURES[f.name];
  const { inst, c } = await playTo(f, { engine, store, dir, cvars });
  try {
    await c.rpc("sim.pauseat", { tic: spec.tic });
    await waitLevelTime(c, spec.tic, { paused: true });
    const results = [];
    for (const v of spec.views) {
      if (v.at) await standAt(c, { ...v.at, force: 1 }, log);
      await sleep(600);
      const file = path.join(os.tmpdir(), `fuactl-picture-${f.name}-${v.name}-${process.pid}.png`);
      if (!(await captureVulkan(c, file))) throw new Error(`${f.name} ${v.name}: no Vulkan capture`);
      results.push({ name: `${f.name} ${v.name}`, ...comparePicture(file, path.join(dir, "pictures", `${f.name}-${v.name}.png`), { update, tolerance }) });
    }
    return results;
  } finally {
    c.close();
    await stopInstance(inst);
  }
}

// [rc4l] A fixture's demo played with GL drawing beside the backend and every walk checked against GL's (fua_vis_check) up to its picture tic.
export async function visCheckFixture(f, opts = {}) {
  return reportCheck(f, { ...opts, check: { fua_dg_standalone: 0, fua_vis_check: 1 }, gl: true, report: "fua_vis_report", parse: parseVisReport,
    describe: (r) => (r.found ? `${r.walked} frames walked beside GL, ${r.differed} differed` : "no walk was checked against GL") });
}

// [rc4l] The numbers fua_copy_report gives: what the render copy was compared against the live game, before the views and after the interpolations, and how much differed.
export function parseCopyReport(text) {
  const before = /copy before views: (\d+) compared, (\d+) differed/.exec(text);
  const blends = /copy blends: (\d+) compared, (\d+) differed/.exec(text);
  return { walked: before ? Number(before[1]) : 0, blends: blends ? Number(blends[1]) : 0,
    differed: (before ? Number(before[2]) : 0) + (blends ? Number(blends[2]) : 0), found: !!before, text };
}

// [rc4l] A fixture's demo played with the render copy held to the live game (fua_copy_check) up to its picture tic.
export async function copyCheckFixture(f, opts = {}) {
  return reportCheck(f, { ...opts, check: { fua_copy_check: 1 }, report: "fua_copy_report", parse: parseCopyReport,
    describe: (r) => (r.found ? `${r.walked} map structures and ${r.blends} blends compared with the live game, ${r.differed} differed` : "the copy was not checked") });
}

// [rc4l] The numbers fua_sprite_report gives: the actors derived beside GL's sprites and how many differed, an actor only one side drew not counting.
export function parseSpriteReport(text) {
  const m = /sprites: (\d+) actors derived beside GL \((\d+) model surfaces\), (\d+) differed/.exec(text);
  return { walked: m ? Number(m[1]) : 0, models: m ? Number(m[2]) : 0, differed: m ? Number(m[3]) : 0, found: !!m, text };
}

// [rc4l] A fixture's demo played with the backend on and standalone, the check keeping GL drawing beside it, every actor's derived sprite or model held to GL's (fua_sprite_check) at its picture tic, paused since GL's frame and the backend's are set up a moment apart, and from the player's own view since a paused teleport turns GL's sprites and not ours (queue #50).
export async function spriteCheckFixture(f, opts = {}) {
  return reportCheck(f, { ...opts, check: { fua_vulkan: 1, fua_dg_standalone: 1, fua_sprite_check: 1 }, gl: true, paused: true, views: (spec) => spec.views.filter((v) => !v.at), report: "fua_sprite_report",
    parse: parseSpriteReport,
    describe: (r) => (r.found ? `${r.walked} actors derived beside GL (${r.models} model surfaces), ${r.differed} differed` : "no actor was checked against GL") });
}

// [rc4l] The numbers fua_psprite_report gives: the frames whose weapon was derived beside GL's and how many differed.
export function parsePSpriteReport(text) {
  const m = /weapon: (\d+) frames derived beside GL \((\d+) quads\), (\d+) differed/.exec(text);
  return { walked: m ? Number(m[1]) : 0, quads: m ? Number(m[2]) : 0, differed: m ? Number(m[3]) : 0, found: !!m, text };
}

// [rc4l] The sprite check's launch for the weapon: every frame's weapon quads held to GL's (fua_psprite_check) at the picture tic.
export async function pspriteCheckFixture(f, opts = {}) {
  return reportCheck(f, { ...opts, check: { fua_vulkan: 1, fua_dg_standalone: 1, fua_psprite_check: 1 }, gl: true, paused: true, views: (spec) => spec.views.filter((v) => !v.at),
    report: "fua_psprite_report", parse: parsePSpriteReport,
    describe: (r) => (r.found ? `${r.walked} frames of the weapon derived beside GL (${r.quads} quads), ${r.differed} differed` : "no weapon was checked against GL") });
}

// [rc4l] The numbers fua_seen_count gives: the lines the walks have mapped and the sectors they have drawn on the live map.
export function parseSeenReport(text) {
  const m = /seen: (\d+) lines mapped, (\d+) sectors drawn/.exec(text);
  return { walked: m ? Number(m[1]) : 0, sectors: m ? Number(m[2]) : 0, differed: 0, found: !!m, text };
}

// [rc4l] A fixture's demo played on Vulkan alone, where no GL walk sets the seen bits, judged by the backend's walk having set them on the live map, which the automap, saves and bots read.
export async function seenCheckFixture(f, opts = {}) {
  return reportCheck(f, { ...opts, check: {}, gl: false, report: "fua_seen_count", parse: parseSeenReport,
    describe: (r) => (r.found ? `${r.walked} lines mapped and ${r.sectors} sectors drawn on the live map` : "the seen bits were not counted") });
}

// [rc4l] Reports added together, view by view.
export function sumReports(reports) {
  const out = { walked: 0, differed: 0, found: false, text: "" };
  for (const r of reports) {
    for (const [k, v] of Object.entries(r)) if (typeof v === "number") out[k] = (out[k] || 0) + v;
    out.found = out.found || r.found;
    out.text = [out.text, r.text].filter(Boolean).join("\n");
  }
  return out;
}

// [rc4l] The part of the frame the HUD check compares: all but the top strip, where the console's notify lines are drawn after the HUD.
export const HUD_BOX = [0, 0.03, 1, 1];

// [rc4l] The tics the HUD check pauses at: the picture tic and the half second before it, where a HUD message fades and the status bar counts.
export function hudTics(spec) {
  return [spec.tic - 30, spec.tic - 15, spec.tic];
}

// [rc4l] A fixture's demo on Vulkan alone, paused at each of hudTics, its frame with the HUD the handoff recorded held to the same frame with the HUD drawn live (fua_hud_record 0): the recording is the live HUD at that tic, to the pixel.
export async function hudCheckFixture(f, { engine, store, dir = fixtureDir(), cvars = {}, log = () => {} } = {}) {
  const spec = PICTURES[f.name];
  // [rc4l] No frame-rate readout, which changes from one capture to the next.
  const { inst, c } = await playTo(f, { engine, store, dir, cvars: { vid_fps: 0, ...cvars } });
  try {
    let compared = 0, differed = 0;
    const worst = [];
    for (const tic of hudTics(spec)) {
      await c.rpc("sim.pauseat", { tic });
      await waitLevelTime(c, tic, { paused: true });
      const frames = {};
      for (const record of [1, 0]) {
        await exec(c, `fua_hud_record ${record}`);
        await sleep(600);
        const file = path.join(os.tmpdir(), `fuactl-hud-${f.name}-${tic}-${record}-${process.pid}.png`);
        if (!(await captureVulkan(c, file))) throw new Error(`${f.name} tic ${tic}: no Vulkan capture`);
        frames[record] = decodePNG(file);
      }
      await exec(c, "fua_hud_record 1");
      // [rc4l] Below the console's notify lines, which are drawn live after the HUD and may change between the two captures.
      const max = boxDiffMax(frames[1], frames[0], HUD_BOX);
      compared++;
      if (max > 0) { differed++; worst.push(`tic ${tic} block ${max.toFixed(2)}`); }
      await c.rpc("sim.resume", {});
    }
    const line = `${compared} paused tics with the recorded HUD held to the live one, ${differed} differed${worst.length ? ` (${worst.join(", ")})` : ""}`;
    return { name: f.name, same: compared > 0 && differed === 0, line, report: { walked: compared, differed, found: compared > 0 } };
  } finally {
    c.close();
    await stopInstance(inst);
  }
}

// [rc4l] What fua_seams prints: each seam entered (a table line with its entries) and the entries a thread its owner does not allow made, with the first such seam.
export function parseSeamReport(text) {
  const total = /^(\d+) violations(?:, the first in (\S+) on the (\w+) thread(?: in (\S+?)[;\s])?)?/m.exec(text);
  const lines = String(text).split(/\r?\n/);
  const entered = lines.filter((l) => /^\s*\S+\/\S+\s/.test(l)).length;
  // [rc4l] fua_seams' own rows of where disallowed threads entered, the most entered first.
  const sites = lines.filter((l) => /^\s+violated /.test(l)).map((l) => l.trim().replace(/^violated /, ""));
  return { walked: entered, differed: total ? Number(total[1]) : 0, first: total && total[2] ? `${total[2]} (${total[3]}${total[4] ? `, ${total[4]}` : ""})` : "", sites, found: !!total, text };
}

// [rc4l] A fixture's demo played to its picture tic on Vulkan alone, judged by fua_seams: no frame touched game data and no tic the main thread's state, outside the handoff.
export async function seamCheckFixture(f, opts = {}) {
  return reportCheck(f, { ...opts, check: {}, gl: false, report: "fua_seams", parse: parseSeamReport,
    describe: (r) => (r.found ? `${r.walked} seams entered, ${r.differed} violations${r.first ? `, the first in ${r.first}` : ""}${r.sites.length ? `; most: ${r.sites.slice(0, 3).join("; ")}` : ""}` : "the seams were not listed") });
}

// [rc4l] The checks a fixture's demo can be played under, each judged by its own report.
export const CHECKS = { vischeck: visCheckFixture, copycheck: copyCheckFixture, spritecheck: spriteCheckFixture, pspritecheck: pspriteCheckFixture, seencheck: seenCheckFixture,
  hudcheck: hudCheckFixture, seamcheck: seamCheckFixture };

// [rc4l] A fixture's demo played with a check's cvars on to its picture tic, then judged by the report the check prints: clean when it compared something and nothing differed; `paused` judges only the frames drawn paused there, from each of the views `views` picks.
async function reportCheck(f, { engine, store, dir = fixtureDir(), cvars = {}, check, gl = false, paused = false, views = (spec) => spec.views, report, parse, describe,
  log = () => {} }) {
  const spec = PICTURES[f.name];
  const { inst, c } = await playTo(f, { engine, store, dir, cvars: { ...check, ...cvars }, gl });
  try {
    let r;
    if (!paused) {
      await waitLevelTime(c, spec.tic);
      r = parse(await execOut(c, report, { maxMs: 6000 }));
    } else {
      await c.rpc("sim.pauseat", { tic: spec.tic });
      await waitLevelTime(c, spec.tic, { paused: true });
      const reports = [];
      for (const v of views(spec)) {
        if (v.at) await standAt(c, { ...v.at, force: 1 }, log);
        await sleep(600);
        // [rc4l] The report clears what it printed, so the frames before the view settled are not judged.
        await execOut(c, report, { maxMs: 6000 });
        await sleep(1000);
        reports.push(parse(await execOut(c, report, { maxMs: 6000 })));
      }
      r = sumReports(reports);
    }
    return { name: f.name, same: r.found && r.walked > 0 && r.differed === 0, line: describe(r), report: r };
  } finally {
    c.close();
    await stopInstance(inst);
  }
}
