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
// The pure parts -- deciding where a camera goes, reading a number out of console output -- are
// separated from the I/O deliberately, because those are the parts that have been wrong and they
// are only testable apart.
import fs from "node:fs";
import path from "node:path";
import { scaled } from "./timeouts.mjs";

// [rc4l] Waits ms milliseconds; every module that waits on an engine takes it from here.
export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// [rc4l] exec's waits for a command that prints a lot or works for a while first (fua_stats, a soak's mark or verdict, a capture's stop).
export const SLOW_COMMAND = { quietMs: 400, maxMs: 20000 };

// ---------------------------------------------------------------------------------------------
// Console, with its output
// ---------------------------------------------------------------------------------------------

// [rc4l] Run a console command and RETURN WHAT IT PRINTED.
//
// console.exec answers {"executed":true} and nothing else, so every command whose whole purpose is
// to print -- a stats dump, a cvar query -- had to be read back by tailing the engine's log file off
// disk and guessing which lines were new. That guess is what made a tool report a position at the
// map origin: it matched a stale line from a previous frame.
//
// The engine already tees console output to the bridge as `out` events, so the answer is simply to
// listen while the command runs. Quiet time rather than a fixed sleep, because a dump of two
// hundred lines arrives over several frames and a fixed wait either truncates it or wastes a
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

// [rc4l] The renderer `dumprenderer` names, or null when it printed none.
export function launchRenderer(text) {
  const m = /renderer=(\w+)/.exec(String(text || ""));
  return m ? m[1] : null;
}

// [rc4l] The API `dumprenderer` says the backend's device runs on ("Vulkan", "WebGPU"), or null when it printed none.
export function launchBackendApi(text) {
  const m = /backend_api=(\w+)/.exec(String(text || ""));
  return m ? m[1] : null;
}

// [rc4l] A cvar's value as the console echoes it ("name" is "value"), or null when it was not echoed.
export function echoedCvar(text, name) {
  const m = new RegExp(`"${name}" is "([^"]*)"`).exec(String(text || ""));
  return m ? m[1] : null;
}

const on = (v) => v === "true" || v === "1";

// [rc4l] The console line whose answer RENDERER_REPORT reads: the renderer, the backend's API, and the two cvars that say whether GL draws beside it.
export const RENDERER_PROBE = "dumprenderer; fua_vulkan; fua_dg_standalone";

// [rc4l] The mode play.mjs names that a launch reports running, from RENDERER_PROBE's answer: no GL is Vulkan or WebGPU alone, GL with the backend on is GL beside it, else GL alone; null when it said nothing.
export function launchModeOf(text) {
  const renderer = launchRenderer(text);
  if (renderer === "vulkan") return launchBackendApi(text) === "WebGPU" ? "webgpu" : "vulkan";
  if (renderer === "opengl") return on(echoedCvar(text, "fua_vulkan")) ? "beside" : "gl";
  return null;
}

// [rc4l] Whether a launch in mode has a GL frame to read: GL alone, or GL beside the backend while it still draws the world (standalone off); else the reason and how to get one.
export function glFrameFor(mode, standalone) {
  if (mode === "gl" || (mode === "beside" && !standalone)) return { ok: true, why: "" };
  if (mode === "beside") return { ok: false, why: "GL is not drawing the world (fua_dg_standalone 1): set it to 0 for a GL frame" };
  return { ok: false, why: `this launch has no GL (${mode ? `${mode} alone` : "no renderer reported"}): relaunch with --gl-beside for GL beside the backend` };
}

// [rc4l] glFrameFor from RENDERER_PROBE's answer.
export function glFrameOf(text) {
  return glFrameFor(launchModeOf(text), on(echoedCvar(text, "fua_dg_standalone")));
}

// [rc4l] Each connection's launch mode, which never changes while it runs; only GL beside is asked again, since its standalone cvar can.
const knownModes = new WeakMap();

// [rc4l] launchModeOf and glFrameOf for a running instance, from at most one console round trip.
export async function rendererReport(c) {
  const known = knownModes.get(c);
  if (known && known !== "beside") return { mode: known, gl: glFrameFor(known, false) };
  const text = await exec(c, RENDERER_PROBE, { quietMs: 300 });
  const mode = launchModeOf(text);
  if (mode) knownModes.set(c, mode);
  return { mode, gl: glFrameOf(text) };
}

// [rc4l] Wait until the instance is playing a level, which a map change leaves for a while; returns the sim.tic reply.
// [rc4l] The engine's frametime distribution over the next `frames` frames, as the perf event reports it.
export async function capturePerf(c, { frames = 300, warmup = 0 } = {}) {
  const done = c.waitEvent("perf", 120000);
  await c.rpc("perf.capture", { frames, warmup });
  return done;
}

// [rc4l] gamestate's GS_INTERMISSION (doomdef.h), the tally between levels that waits for a key.
export const GS_INTERMISSION = 1;

// [rc4l] Waits for the instance to be in a level; pressThrough presses use at an intermission, as a player leaves the tally, which a level change by an exit waits on.
export async function waitInLevel(c, timeoutMs = scaled(20000), { pressThrough = false, pollMs = 200 } = {}) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const t = await c.rpc("sim.tic").catch(() => ({}));
    if (t.inlevel) return t;
    if (pressThrough && t.gamestate === GS_INTERMISSION) {
      await exec(c, "+use", { quietMs: 60 });
      await exec(c, "-use", { quietMs: 60 });
    }
    await sleep(pollMs);
  }
  throw new Error("instance never reached GS_LEVEL");
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

// [rc4l] `gametic`, and specifically not `leveltime`.
//
// The field name was guessed here at first, and a guess that misses yields NaN rather than an error:
// the wait then never advanced and every capture died with "waited 30000ms and never got there".
// gametic is also the one that survives a map change -- leveltime restarts at zero, so a wait
// spanning `map MAP01` would sit forever watching a counter that had gone backwards.
export function ticOf(reply) {
  // Not `Number(reply && reply.gametic)`: a null reply short-circuits to null, and Number(null) is
  // ZERO, so a dead connection would have read as "the game is at tic 0" and waited forever.
  const n = reply == null ? NaN : Number(reply.gametic);
  if (!Number.isFinite(n)) throw new Error(`sim.tic had no gametic: ${JSON.stringify(reply)}`);
  return n;
}

async function tic(c) {
  return ticOf(await c.rpc("sim.tic", {}));
}

// ---------------------------------------------------------------------------------------------
// A world that holds still
// ---------------------------------------------------------------------------------------------

// [rc4l] Every cvar here is one that has silently invalidated a measurement.
//
// They are applied together, always, because the failures they cause do not look like the cvar that
// caused them: freelook off looks like a weapon that will not aim down, monsters on look like a
// renderer drawing geometry in the wrong place after the player got shoved four units sideways
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

// Reset to a known level and hold it still. A fresh level per capture because what a shot leaves
// behind accumulates for the whole level: the second capture in a session contains the first one's
// leavings too, and there is no telling afterwards which one was being judged.
export async function sandbox(c, { map = null, vulkan = true } = {}) {
  if (map) { await exec(c, `map ${map}`); await waitTics(c, 35); }
  for (const cmd of SANDBOX) await exec(c, cmd, { quietMs: 60 });
  if (vulkan) { await exec(c, "gl_wallmesh 1"); await exec(c, "fua_vulkan 1"); await waitTics(c, 25); }
}

// ---------------------------------------------------------------------------------------------
// Firing, and framing what you want to look at
// ---------------------------------------------------------------------------------------------

// The BFG spends about a second winding up before the ball leaves; a rocket is gone on the next tic.
export const HOLD_TICS = (weapon) => (/BFG/i.test(weapon) ? 110 : 8);

// [rc4l] settleTics is how long the world is allowed to age before anyone looks at it.
//
// Not a constant, because some of what a shot leaves behind is TRANSIENT: anything on a fader is
// over within a few seconds, so a capture that waits a comfortable 120 tics for everything to
// settle photographs a wall after the thing it was meant to show has already gone, and reports it
// missing when it was merely over.
export async function fire(c, { x, y, z, yaw, pitch, weapon = "RocketLauncher", settleTics = 120 }) {
  await c.rpc("player.setpos", { x, y, z, angle: yaw, pitch });
  await exec(c, `use ${weapon}`);
  await waitTics(c, 25);
  await exec(c, "+attack", { quietMs: 60 });
  await waitTics(c, HOLD_TICS(weapon));
  await exec(c, "-attack", { quietMs: 60 });
  await waitTics(c, settleTics);
}

// [rc4l] Where to stand to look at a point. Pure, because the arithmetic has been wrong twice.
//
// Backing off along the firing direction and rising a little puts the target in frame at an angle
// that shows how it sits ACROSS a junction rather than flat on one face, which is the whole point
// of looking at it. `frac` shrinks the standoff for the retry when the full one is inside a wall.
export function viewpoint(target, yaw, { back = 112, up = 56, frac = 1 } = {}) {
  const b = back * frac, u = up * frac;
  const r = (yaw * Math.PI) / 180;
  return {
    x: target.x - b * Math.cos(r),
    y: target.y - b * Math.sin(r),
    z: target.z + u,
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
export async function placeCamera(c, target, yaw, opts = {}) {
  for (const frac of [1, 0.75, 0.55, 0.4, 0.28]) {
    const v = viewpoint(target, yaw, { ...opts, frac });
    try {
      await c.rpc("player.setpos", { x: v.x, y: v.y, z: v.z, angle: v.yaw, pitch: v.pitch });
      return { ...v, frac };
    } catch { /* solid there -- try closer */ }
  }
  return null;
}
