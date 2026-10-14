// [rc4l] A build you can actually walk around in, with the Vulkan view live in the window.
//
// One window. The backend renders into a disabled child covering the engine's client area, so the
// pixels on screen are Vulkan while the mouse and keyboard reach the engine exactly as always.
//
// This was a shell script for a long time, which meant the one launch path a human uses was the one
// path with no tests. The argument-ordering bug that shipped a "no monsters" build full of monsters
// lived here, under a comment claiming the opposite.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { launchInstance, stopInstance, describeExit } from "./launch.mjs";
import { sessionFileFor, clearPortSessions, standAt, holdingLaunchFile } from "./shot.mjs";

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// [rc4l] Every one of these is set at LAUNCH, not from the console afterwards.
//
// sv_nomonsters only takes effect at level start, and freelook being off is indistinguishable at
// the window from a backend that will not aim -- a play session came up unable to look up or down
// at all and it read as a rendering fault.
//
// sv_freelook is a MASK cvar over DF_NO_FREELOOK|DF_YES_FREELOOK: `1` sets the NO bit and turns
// freelook OFF. It is `2` that enables it. sv_nofreeaim decides whether a projectile follows the
// crosshair or is auto-aimed at whatever is nearest -- the difference between hitting a wall/floor
// join on purpose and hitting it by luck.
export function playCvars({ gl = false, sideBySide = false, rt = false, monsters = false, lock = false, extra = {} } = {}) {
  const cv = {
    sv_nofreelook: 0, sv_freelook: 2, freelook: 1,
    sv_nofreeaim: 0, autoaim: 0,
    // [rc4l] The BFG has its OWN free-aim bit and does not follow sv_nofreeaim.
    //
    // A_FireBFG passes `!(dmflags2 & DF2_YES_FREEAIMBFG)` as its nofreeaim argument, so with this
    // bit clear the ball is auto-aimed no matter what the general free-aim settings say -- the shot
    // leaves level and lands somewhere nobody asked about, which is useless for testing a mark at a
    // floor or ceiling join. It reads as "freeaim is broken" while every other free-aim cvar checks
    // out, which is exactly how it survived being "fixed" three times.
    sv_bfgfreeaim: 1,
  };
  if (!gl) cv.fua_vulkan = 1;
  // Its own window beside the engine's instead of embedded, so both renderers are on screen at
  // once. Easier to point at a difference than toggling and trying to remember the other one.
  // [rc4l] GL draws whenever the backend's window is its own, so nothing else is needed for both to be live.
  if (sideBySide) cv.fua_dg_embed = 0;
  // Ray-traced reflections must be on at LAUNCH: the acceleration structure is built at scene
  // upload, which has already happened by the time a level is playable, so setting it later leaves
  // nothing for the rays to hit.
  if (rt) cv.fua_dg_rtmirrors = 1;
  // Being shot at halfway through showing someone a texture seam is pure friction. Monsters go back
  // when the thing to look at IS a monster sprite.
  if (!monsters) cv.sv_nomonsters = 1;
  // [rc4l] A locked launch never has the focus, and the background pause would hold its world at leveltime 1 with the weapon still lowered.
  if (lock) cv.i_pauseinbackground = 0;
  // [rc4l] --cvar's, last, so a launch can set what it needs before the map loads.
  return { ...cv, ...extra };
}

// [rc4l] The engine's own download store (M_GetFuaUserPath + "/pwads"), where `fua_download` puts catalogue wads.
export function defaultPwadStore(platform = process.platform, env = process.env, home = os.homedir()) {
  if (platform === "win32") return path.join(env.LOCALAPPDATA || path.join(home, "AppData/Local"), "ForkUnderA/pwads");
  if (platform === "darwin") return path.join(home, "Library/Application Support/ForkUnderA/pwads");
  return path.join(home, ".config/ForkUnderA/pwads");
}

// [rc4l] Resolve a catalogue entry to its file list, so playing what the HOST tab offers does not
// mean copying six pk3 paths out of a JSON file by hand. A missing file is NAMED rather than
// quietly dropped: a wad list that is one short loads a map with no textures and looks like a
// renderer bug.
export function resolvePreset(catalogueDir, storeDir, id, variantId) {
  const meta = JSON.parse(fs.readFileSync(path.join(catalogueDir, id, "addon.json"), "utf8"));
  // [rc4l] An entry with no variants carries its files at the top level, and is its own only variant.
  const variants = meta.variants || [{ id: "default", default: true, files: meta.files }];
  const variant = variantId
    ? variants.find((v) => v.id === variantId)
    : variants.find((v) => v.default) || variants[0];
  if (!variant) throw new Error(`no such variant: ${variantId}`);
  const found = [], missing = [];
  for (const f of variant.files || []) {
    const p = path.join(storeDir, f.name);
    (fs.existsSync(p) ? found : missing).push(p);
  }
  if (missing.length) throw new Error(`missing, fetch with fua_download: ${missing.join(" ")}`);
  return found;
}

// Wait for a playable level, bounded and loudly.
//
// The wait this replaces polled for a session file and had no opinion about the launch dying: when
// the engine exited early -- port still held, a missing pk3, a bad map name -- the file never
// appeared and it spun until something else timed out, with the reason already printed and going
// nowhere. A launch that is not going to work should say so in seconds.
async function waitInLevel(inst, { timeoutMs = 90000, log = () => {} } = {}) {
  const { BridgeClient } = await import("./client.mjs");
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (inst.proc.exitCode != null) throw new Error(`engine exited ${describeExit(inst.proc.exitCode)} before reaching a level`);
    try {
      const c = new BridgeClient();
      await c.connect(inst.port, { token: inst.token, timeoutMs: 1500 });
      await c.waitHello(3000);
      const st = await c.rpc("sim.tic", {});
      c.close();
      if (st && st.inlevel) return true;
    } catch { /* not up yet */ }
    await sleep(250);
  }
  throw new Error(`no playable level after ${Math.round(timeoutMs / 1000)}s`);
}

// [rc4l] Cheats are TOGGLES, so issuing one is not the same as turning it on.
//
// `god` prints "Degreelessness Mode ON" or "OFF" and flips whichever way it was. Firing it blind at
// a session that already had it on turns it OFF -- which is how a capture run ended in a death
// screen with the fault nowhere in it, after a script that "enabled god mode" twice.
//
// The general shape: do not assume a setting, OBSERVE it. Issue the toggle, read what the engine
// says happened, and issue it again if that was the wrong direction.
export async function ensureCheat(c, { command, onText, offText }, { exec }) {
  const first = await exec(c, command, { quietMs: 120 });
  if (onText.test(first)) return { command, on: true, flipped: false };
  if (offText.test(first)) {
    const second = await exec(c, command, { quietMs: 120 });
    return { command, on: onText.test(second), flipped: true };
  }
  return { command, on: null, flipped: false };   // engine said neither; report it rather than guess
}

export const CHEATS = [
  { command: "god", onText: /Degreelessness Mode ON/i, offText: /Degreelessness Mode OFF/i },
  { command: "notarget", onText: /MDK Mode ON|notarget.*ON/i, offText: /MDK Mode OFF|notarget.*OFF/i },
];

// [rc4l] Doom echoes booleans as "true"/"false" or as "1"/"0" depending on the cvar's type, and
// which one a given cvar uses is not something to be memorised. The FIRST run of this check failed
// on sv_nofreeaim purely because the expectation said "false" and the engine said "0" -- a correct
// setting reported as broken, which is the way to lose trust in a check fastest.
export function sameCvarValue(actual, expected) {
  if (actual == null) return false;
  const norm = (v) => {
    const s = String(v).trim().toLowerCase();
    if (s === "true") return "1";
    if (s === "false") return "0";
    return s;
  };
  return norm(actual) === norm(expected);
}

// [rc4l] What the world was actually set to, read back rather than assumed.
//
// Every one of these has silently invalidated a measurement, and none of them announces itself when
// it is wrong: freelook off looks like a weapon that will not aim down, monsters on look like a
// renderer drawing in the wrong place after the player was shoved sideways mid-capture. A cvar
// query prints `"name" is "value"`, which is exactly what is needed and was being thrown away
// before console output could be captured.
export async function verifyWorld(c, { exec }) {
  const want = {
    sv_nomonsters: "true",
    sv_freelook: "2",      // the YES bit; 1 is DF_NO_FREELOOK and turns freelook OFF
    sv_nofreeaim: "false",
    autoaim: "0",
    sv_bfgfreeaim: "true",   // its own dmflags2 bit; the general free-aim cvars do not cover it
  };
  const report = [];
  for (const [name, expected] of Object.entries(want)) {
    const out = await exec(c, name, { quietMs: 120 });
    const m = /"[^"]+" is "([^"]*)"/.exec(out);
    const actual = m ? m[1] : null;
    report.push({ name, expected, actual, ok: sameCvarValue(actual, expected) });
  }
  return report;
}

// [rc4l] The engine arguments past the cvars: each wad as -file, and -vulkan when the launch runs with no GL at all, or -webgpu for the same backend on WebGPU.
export function playArgs(files, { noGl = false, webgpu = false } = {}) {
  return [...files.flatMap((f) => ["-file", f]), ...(webgpu ? ["-webgpu"] : noGl ? ["-vulkan"] : [])];
}

// [rc4l] Whether an engine still answers on port once a closing one has had graceMs to go.
export async function portStillAnswers(port, { connect = bridgeConnect, graceMs = 2000, pollMs = 200 } = {}) {
  for (const deadline = Date.now() + graceMs; ;) {
    try { (await connect(Number(port), null)).close(); } catch { return false; }
    if (Date.now() >= deadline) return true;
    await sleep(pollMs);
  }
}

export async function play(opts = {}, { connect = bridgeConnect, graceMs = 2000 } = {}) {
  const port = opts.port || 7900;
  // [rc4l] An engine still answering on the port keeps its session files, since clearing them left it with no token to be quit by.
  if (await portStillAnswers(port, { connect, graceMs })) throw new Error(`port ${port} is in use: quit it first (fuactl quit --port ${port}) or launch with --restart`);
  if (!opts.sessionFile) return launchChecked(opts, port);
  clearPortSessions(opts.sessionFile, port);
  return holdingLaunchFile(opts.sessionFile, port, () => launchChecked(opts, port));
}

// [rc4l] play's launch, its world checked and the player stood at its camera, with its session files written once it is.
async function launchChecked(opts, port) {
  const files = opts.preset
    ? resolvePreset(opts.catalogueDir, opts.storeDir, opts.preset, opts.variant)
    : (opts.file ? String(opts.file).split(",").map((s) => s.trim()) : []);

  const inst = await launchInstance({
    port,
    // [rc4l] A caller-chosen token, forwarded. play used to mint its own and print it, so a scripted
    // launch could only learn it by parsing stdout -- which is buffered, so the first several seconds
    // of a run had a live engine and no way to address it. .play-session covers the interactive case
    // and is written by whichever launch went last, which is the wrong answer when two are up.
    token: opts.token,
    map: opts.map || "MAP01",
    iwad: opts.iwad || "doom2.wad",
    // [rc4l] Hands-on unless told otherwise -- but --lock exists because MY OWN launches must use
    // it. A hands-on instance takes the foreground and reads the mouse, so a stray movement while a
    // capture is running both perturbs the measurement and yanks the user's cursor into a game they
    // did not ask to be playing.
    allowOsInput: !opts.lock,
    // [rc4l] --res, forwarded. Without this it was accepted and silently dropped.
    width: opts.width, height: opts.height,
    extraArgs: playArgs(files, { noGl: opts.noGl, webgpu: opts.webgpu }),
    cvars: playCvars(opts),
    engine: opts.engine,
  });

  await waitInLevel(inst);

  // [rc4l] And now CHECK, rather than trust the command line.
  //
  // Every setting here has been wrong at least once while the launch reported success, and each
  // time it was discovered by the failure it caused rather than by anyone looking: a build "with no
  // monsters" full of monsters, a session that could not look up or down, a god-mode script that
  // turned god mode off. Reading them back costs one round trip and turns all of that into a line
  // of output at launch.
  const { BridgeClient } = await import("./client.mjs");
  const cap = await import("./capture.mjs");
  const c = new BridgeClient();
  await c.connect(inst.port, { token: inst.token });
  await c.waitHello();
  let world;
  try {
    const cheats = [];
    for (const ch of CHEATS) cheats.push(await ensureCheat(c, ch, cap));
    world = { cheats, cvars: await verifyWorld(c, cap) };
    // [rc4l] opts.at (x, y, z, angle, pitch) stands the player there before anyone is told the instance is up.
    if (opts.at) await standAt(c, opts.at);
  } finally { c.close(); }
  inst.world = world;

  if (opts.sessionFile) {
    const text = `PORT=${inst.port}\nTOKEN=${inst.token}\nPID=${inst.pid}\n${inst.engine ? `ENGINE=${inst.engine}\n` : ""}`;
    fs.writeFileSync(opts.sessionFile, text);
    fs.writeFileSync(sessionFileFor(opts.sessionFile, inst.port), text);
  }
  return inst;
}

export { stopInstance };

// [rc4l] A locked launch handed to body with its bridge connected and always torn down; a crash comes back as the error naming its crash log.
export async function withLockedInstance(opts, body, { launch = play, connect = bridgeConnect, stop = stopInstance } = {}) {
  let inst = null;
  let c = null;
  try {
    inst = await launch({ ...opts, lock: true });
    c = await connect(inst.port, inst.token);
    return await body(c, inst);
  } catch (e) {
    const died = inst && inst.log && fs.existsSync(`${inst.log}.crash`);
    throw new Error(died ? `engine crashed -- ${inst.log}.crash` : e.message);
  } finally {
    if (c) c.close();
    if (inst) await stop(inst);
  }
}

// [rc4l] A bridge connection that has said hello, or a throw when nothing answers on port.
async function bridgeConnect(port, token) {
  const { BridgeClient } = await import("./client.mjs");
  const c = new BridgeClient();
  try { await c.connect(port, { token }); await c.waitHello(); return c; } catch (e) { c.close(); throw e; }
}

// [rc4l] Quit the instance on port by its token and wait for the port to close: "stopped", "absent" when nothing answered, or "running" when it would not go.
export async function quitInstance({ port, token = null }, { connect = bridgeConnect, timeoutMs = 10000, pollMs = 200 } = {}) {
  let c;
  try { c = await connect(Number(port), token); } catch { return "absent"; }
  let failed = false;
  await c.rpc("console.exec", { text: "quit" }).catch(() => { failed = true; });
  c.close();
  // [rc4l] A failed quit on a port that still answers was refused (a caller without its token), which waiting cannot change; one that closed has gone.
  if (failed) { try { (await connect(Number(port), token)).close(); return "refused"; } catch { return "stopped"; } }
  for (const deadline = Date.now() + timeoutMs; Date.now() < deadline;) {
    await sleep(pollMs);
    try { (await connect(Number(port), token)).close(); } catch { return "stopped"; }
  }
  return "running";
}
