// [rc4l] Driving the engine to look at something, as functions instead of shell scripts.
//
// Every check of a rendering fault is the same five steps -- reset the level, hold the world still,
// get in position, do the thing, capture both renderers from ONE camera -- and for a long time each
// of them lived in a separate .sh file in this directory, with a sixth written from scratch each
// time a check needed a shape none of the five had. That is how a comparison ends up run at a
// slightly different camera than the one before it, which is the comparison that cannot be made.
//
// So: one module, and `fuactl` subcommands over it. The rule this exists to enforce is that there is
// no second way to drive the engine. A check that needs something new adds a function here and a
// subcommand in cli.mjs, where it gets a name, a test, and a chance of being used again.
//
// The pure parts -- deciding where a camera goes, reading a mark out of console output -- are
// separated from the I/O deliberately, because those are the parts that have been wrong and they
// are only testable apart.
import fs from "node:fs";
import path from "node:path";
import * as ui from "./ui.mjs";

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---------------------------------------------------------------------------------------------
// Console, with its output
// ---------------------------------------------------------------------------------------------

// [rc4l] Run a console command and RETURN WHAT IT PRINTED.
//
// console.exec answers {"executed":true} and nothing else, so every command whose whole purpose is
// to print -- fua_walldecals, fua_flatdecals, a cvar query -- had to be read back by tailing the
// engine's log file off disk and guessing which lines were new. That guess is what made a tool
// report a decal at the map origin: it matched a stale line from a previous frame.
//
// The engine already tees console output to the bridge as `out` events, so the answer is simply to
// listen while the command runs. Quiet time rather than a fixed sleep, because a dump of two
// hundred decals arrives over several frames and a fixed wait either truncates it or wastes a
// second on every one-line command.
export async function exec(c, text, { quietMs = 220, maxMs = 4000 } = {}) {
  let out = "";
  let last = Date.now();
  const off = c.onEvent((ev, data) => {
    if (ev === "out" && typeof data.text === "string") { out += data.text; last = Date.now(); }
  });
  try {
    const started = Date.now();
    await c.rpc("console.exec", { text });
    for (;;) {
      await sleep(40);
      if (Date.now() - last >= quietMs) break;
      if (Date.now() - started >= maxMs) break;
    }
  } finally { off(); }
  return out;
}

// Advance the simulation by n tics, measured against the engine's own clock rather than wall time.
// A busy frame or a stalled window makes a wall-clock sleep mean a different number of tics each
// run, which is how a projectile gets captured mid-flight in one run and landed in the next.
export async function waitTics(c, n, { timeoutMs = 30000 } = {}) {
  const start = await tic(c);
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    await sleep(30);
    const now = await tic(c);
    if (now - start >= n) return now - start;
    if (Date.now() > deadline) throw new Error(`waited ${timeoutMs}ms for ${n} tics and never got there`);
  }
}

async function tic(c) {
  const r = await c.rpc("sim.tic", {});
  return Number(r && (r.tic ?? r.tics ?? r.value ?? r));
}

// ---------------------------------------------------------------------------------------------
// A world that holds still
// ---------------------------------------------------------------------------------------------

// [rc4l] Every cvar here is one that has silently invalidated a measurement.
//
// They are applied together, always, because the failures they cause do not look like the cvar that
// caused them: freelook off looks like a weapon that will not aim down, monsters on look like a
// renderer drawing a decal in the wrong place after the player got shoved four units sideways
// between the two halves of a pair.
export const SANDBOX = [
  // Nothing damages, chases or shoves the observer. A monster moves the camera between the two
  // captures of a pair, so the pair is no longer one camera -- and can end it in a death screen.
  "god", "notarget", "kill monsters",
  // sv_freelook is a MASK over DF_NO_FREELOOK|DF_YES_FREELOOK, so `sv_freelook 1` sets the NO bit
  // and turns freelook OFF. With it off the engine centres the view every tic: pitch is silently
  // discarded, and every shot meant for a floor or ceiling join is fired level instead. Whole
  // rounds of "aim at the wall base" testing were actually fired straight ahead because of this.
  "sv_nofreelook 0", "sv_freelook 2", "freelook 1",
  // And projectiles that go where the crosshair points rather than at whatever is nearest, which is
  // the difference between hitting a junction on purpose and hitting it by luck.
  "sv_nofreeaim 0", "autoaim 0",
  // The BFG does NOT follow sv_nofreeaim: A_FireBFG reads its own dmflags2 bit, so without this the
  // ball is auto-aimed however the general free-aim settings are set, and a shot meant for a
  // wall/floor join leaves level entirely.
  "sv_bfgfreeaim 1",
  // The window is usually not focused while a capture runs.
  "i_pauseinbackground 0",
  "give weapons", "give ammo",
];

// Reset to a known level and hold it still. A fresh level per capture because decals accumulate for
// the whole level: the second capture in a session contains the first one's marks too, and there is
// no telling afterwards which blob was being judged.
export async function sandbox(c, { map = null, vulkan = true } = {}) {
  if (map) { await exec(c, `map ${map}`); await waitTics(c, 35); }
  for (const cmd of SANDBOX) await exec(c, cmd, { quietMs: 60 });
  if (vulkan) { await exec(c, "gl_wallmesh 1"); await exec(c, "fua_vulkan 1"); await waitTics(c, 25); }
}

// ---------------------------------------------------------------------------------------------
// Capturing
// ---------------------------------------------------------------------------------------------

// A matched pair from ONE camera: the Vulkan backend and GL, with nothing moving in between.
export async function pair(c, tag, { engineBin, outDir = "F:/ForkUnderA/dist-windows/sweep" } = {}) {
  const vk = path.posix.join(outDir, `${tag}_vk.png`);
  for (const f of [vk, path.posix.join(outDir, `${tag}_gl.png`)]) {
    try { fs.rmSync(f, { force: true }); } catch { /* not there is fine */ }
  }
  await exec(c, `fua_diligent_shot ${vk}`);
  await waitTics(c, 8);
  await exec(c, "fua_vulkan 0");
  await waitTics(c, 8);
  const gl = await ui.screenshot(c, engineBin, `sweep/${tag}_gl`);
  await waitTics(c, 8);
  await exec(c, "fua_vulkan 1");
  await waitTics(c, 4);
  return { vk, gl: gl.path };
}

// ---------------------------------------------------------------------------------------------
// Making a mark, and finding it again
// ---------------------------------------------------------------------------------------------

// The BFG spends about a second winding up before the ball leaves; a rocket is gone on the next tic.
export const HOLD_TICS = (weapon) => (/BFG/i.test(weapon) ? 110 : 8);

export async function fire(c, { x, y, z, yaw, pitch, weapon = "RocketLauncher" }) {
  await c.rpc("player.setpos", { x, y, z, angle: yaw, pitch });
  await exec(c, `use ${weapon}`);
  await waitTics(c, 25);
  await exec(c, "+attack", { quietMs: 60 });
  await waitTics(c, HOLD_TICS(weapon));
  await exec(c, "-attack", { quietMs: 60 });
  await waitTics(c, 120);   // the projectile flies, lands, and its decals settle
}

// [rc4l] Read the newest mark out of what the decal dumps printed. Pure, so it is testable.
//
// The wall dump lists the newest entries last. The flat dump's counters are never reset, so its
// "first emitted" line keeps its last value forever -- an all-zero position there means no flat
// decal has ever been emitted, not one at the map origin. Taking it literally aimed a camera into
// the void and produced a screenshot of nothing that read as a missing-geometry fault.
export function parseMark(consoleText) {
  let mark = null;
  for (const line of String(consoleText).split(/\r?\n/)) {
    const wall = line.match(/at \(([-\d.]+), ([-\d.]+)\)\s+base z [-\d.]+ \+ upOff [-\d.]+ = ([-\d.]+)/);
    if (wall) { mark = { x: +wall[1], y: +wall[2], z: +wall[3], on: "wall" }; continue; }
    const flat = line.match(/first emitted: at \(([-\d.]+), ([-\d.]+), ([-\d.]+)\)/);
    if (flat && !(+flat[1] === 0 && +flat[2] === 0)) {
      mark = { x: +flat[1], y: +flat[2], z: +flat[3], on: "flat" };
    }
  }
  return mark;
}

export async function findMark(c) {
  const text = (await exec(c, "fua_walldecals")) + "\n" + (await exec(c, "fua_flatdecals"));
  return parseMark(text);
}

// [rc4l] Where to stand to look at a mark. Pure, because the arithmetic has been wrong twice.
//
// Backing off along the firing direction and rising a little puts the mark in frame at an angle
// that shows how it sits ACROSS a junction rather than flat on one face, which is the whole point
// of looking at it. `frac` shrinks the standoff for the retry when the full one is inside a wall.
export function viewpoint(mark, yaw, { back = 112, up = 56, frac = 1 } = {}) {
  const b = back * frac, u = up * frac;
  const r = (yaw * Math.PI) / 180;
  return {
    x: mark.x - b * Math.cos(r),
    y: mark.y - b * Math.sin(r),
    z: mark.z + u,
    yaw,
    pitch: (Math.atan2(u, b) * 180) / Math.PI,   // positive pitch looks DOWN
  };
}

// [rc4l] Place the camera, closing in until it fits somewhere a player could actually be.
//
// player.setpos refuses solid destinations, so a refusal is information rather than an error: it
// means the standoff put the camera inside geometry. Forcing it instead photographs the inside of a
// wall, and shots taken that way look exactly like missing geometry -- they have cost a diagnosis
// each. Closing in keeps the same viewing angle and only shortens the distance.
export async function placeCamera(c, mark, yaw, opts = {}) {
  for (const frac of [1, 0.75, 0.55, 0.4, 0.28]) {
    const v = viewpoint(mark, yaw, { ...opts, frac });
    try {
      await c.rpc("player.setpos", { x: v.x, y: v.y, z: v.z, angle: v.yaw, pitch: v.pitch });
      return { ...v, frac };
    } catch { /* solid there -- try closer */ }
  }
  return null;
}
