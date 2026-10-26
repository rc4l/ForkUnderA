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
import { summarizeCrash } from "./crashes.mjs";
import { sessionFileFor, clearPortSessions, standAt, holdingLaunchFile } from "./shot.mjs";
import { sleep } from "./capture.mjs";


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
// [rc4l] A launch's renderers, as players run it unless it asks otherwise: Vulkan alone, GL alone (gl), the backend beside GL in one process for comparing the two (glBeside, which side by side implies), or WebGPU.
export const MODES = Object.freeze({ vulkan: "vulkan", gl: "gl", beside: "beside", webgpu: "webgpu" });
export function launchMode({ gl = false, glBeside = false, sideBySide = false, webgpu = false } = {}) {
  if (webgpu) return MODES.webgpu;
  if (gl) return MODES.gl;
  return glBeside || sideBySide ? MODES.beside : MODES.vulkan;
}

// [rc4l] How a mode is said at launch, so a session never leaves which renderers it runs to be guessed.
export function describeMode(mode, sideBySide = false) {
  if (mode === MODES.gl) return "GL alone";
  if (mode === MODES.webgpu) return "WebGPU alone (no GL), as the browser runs it";
  if (mode === MODES.beside) return `GL with the Vulkan backend beside it${sideBySide ? " in its own window" : ""}, for comparing the two`;
  return "Vulkan alone (no GL), as players run it";
}

export function playCvars({ gl = false, glBeside = false, sideBySide = false, rt = false, monsters = false, lock = false, extra = {} } = {}) {
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
  if (sideBySide && launchMode({ gl, glBeside, sideBySide }) === MODES.beside) cv.fua_dg_embed = 0;
  // Ray-traced reflections must be on at LAUNCH: the acceleration structure is built at scene
  // upload, which has already happened by the time a level is playable, so setting it later leaves
  // nothing for the rays to hit.
  if (rt) cv.fua_dg_rtmirrors = 1;
  // Being shot at halfway through showing someone a texture seam is pure friction. Monsters go back
  // when the thing to look at IS a monster sprite.
  if (!monsters) cv.sv_nomonsters = 1;
  // [rc4l] A locked launch has the focus only when Windows lets it take it, and without it the background pause holds its world at leveltime 1 and the window presents nothing, so what a frame counts reads zero at random.
  if (lock) { cv.i_pauseinbackground = 0; cv.fua_render_in_background = 1; }
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
//
// [rc4l] Each probe's connection is closed whatever happens to it, since a probe left open after a timeout holds a reader thread in the engine until fuactl exits.
export async function waitInLevel(inst, { timeoutMs = 90000, pollMs = 250, connect = (port, token) => bridgeConnect(port, token, { connectMs: 1500, helloMs: 3000 }) } = {}) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (inst.proc.exitCode != null) throw new Error(`engine exited ${describeExit(inst.proc.exitCode)} before reaching a level`);
    let c = null;
    try {
      c = await connect(inst.port, inst.token);
      const st = await c.rpc("sim.tic", {});
      if (st && st.inlevel) return true;
    } catch { /* not up yet */ }
    finally { if (c) c.close(); }
    await sleep(pollMs);
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

// [rc4l] The cheats a launch turns on: god and notarget, or god alone when targetable, since a fight measured under notarget is monsters that never hear the gun.
export function playCheats({ targetable = false } = {}) {
  return targetable ? CHEATS.filter((ch) => ch.command !== "notarget") : CHEATS;
}

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

// [rc4l] The render size, render scale and field of view an instance shows, which frame a picture as much as its camera does.
export async function readView(c, { exec }) {
  const { width, height } = await c.rpc("input.windowpoint", { x: 0, y: 0 });
  const value = async (name) => { const m = /"[^"]+" is "([^"]*)"/.exec(await exec(c, name, { quietMs: 120 })); return m ? Number(m[1]) : null; };
  return { width, height, scale: await value("vid_scalefactor"), fov: await value("fov") };
}

// [rc4l] The window client size vid_setsize takes for a view: its render size over its render scale.
export function windowForView({ width, height, scale }) {
  const s = scale > 0 ? scale : 1;
  return { w: Math.round(width / s), h: Math.round(height / s) };
}

// [rc4l] The view a --res window size is at a render scale, since --res sizes the window and a view is what renders in it.
export function viewForWindow(w, h, scale) {
  const s = scale > 0 ? scale : 1;
  return { width: Math.round(w * s), height: Math.round(h * s) };
}

// [rc4l] Whether a view read back is the one asked for, its size to the pixel a scale rounds by, and its scale and FOV when it names them.
export function sameView(want, got) {
  const near = (a, b) => Math.abs(a - b) <= 1;
  return near(got.width, want.width) && near(got.height, want.height)
    && (want.scale == null || got.scale === want.scale) && (want.fov == null || got.fov === want.fov);
}

// [rc4l] Sets an instance to a view (scale and FOV, then the window through vid_setsize) and reads back what it got, since a launch's size snaps to a mode the engine knows and 900x900 came up 1280x960.
export async function fitView(c, view, cap, { tries = 20, now = null } = {}) {
  let got = now || await readView(c, cap);
  if (sameView(view, got)) return { want: view, got, ok: true };
  if (view.scale != null) await cap.exec(c, `vid_scalefactor ${view.scale}`, { quietMs: 120 });
  if (view.fov != null) await cap.exec(c, `fov ${view.fov}`, { quietMs: 120 });
  const { w, h } = windowForView(view);
  await cap.exec(c, `vid_setsize ${w} ${h}`, { quietMs: 120 });
  for (let i = 0; i < tries && !sameView(view, got = await readView(c, cap)); i++) await cap.sleep(100);
  return { want: view, got, ok: sameView(view, got) };
}

// [rc4l] The engine arguments past the cvars: each wad as -file, then the mode's own, so the launch never rests on the engine's default or the config's vid_backend.
export function playArgs(files, mode = MODES.vulkan) {
  const own = { [MODES.vulkan]: "-vulkan", [MODES.webgpu]: "-webgpu", [MODES.gl]: "-gl", [MODES.beside]: "-gl" }[mode];
  return [...files.flatMap((f) => ["-file", f]), own];
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
    iwad: opts.iwad,
    // [rc4l] Hands-on unless told otherwise -- but --lock exists because MY OWN launches must use
    // it. A hands-on instance takes the foreground and reads the mouse, so a stray movement while a
    // capture is running both perturbs the measurement and yanks the user's cursor into a game they
    // did not ask to be playing.
    allowOsInput: !opts.lock,
    // [rc4l] --res, forwarded. Without this it was accepted and silently dropped.
    width: opts.width, height: opts.height,
    extraArgs: playArgs(files, launchMode(opts)),
    cvars: playCvars(opts),
    engine: opts.engine,
  });

  return stoppingOnFailure(inst, async () => {
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
      for (const ch of playCheats(opts)) cheats.push(await ensureCheat(c, ch, cap));
      world = { cheats, cvars: await verifyWorld(c, cap), view: null, mode: null };
      // [rc4l] The renderers the engine reports running, held to the mode asked for, since the launch's flags are only a request.
      const want = launchMode(opts);
      const got = cap.launchModeOf(await cap.exec(c, cap.RENDERER_PROBE, { quietMs: 300 }));
      world.mode = { want, got, ok: got === want };
      // [rc4l] The --res window or the view a spot was seen in, set and read back, since the ini's size snaps to a known mode.
      // [rc4l] An engine older than input.windowpoint (an old binary under A/B) launches unsized rather than not at all.
      const now = (opts.width && opts.height) || opts.view ? await readView(c, cap).catch(() => null) : null;
      if (now) {
        const view = opts.width && opts.height ? viewForWindow(Number(opts.width), Number(opts.height), now.scale) : opts.view;
        world.view = await fitView(c, view, cap, { now });
      }
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
  });
}

// [rc4l] The error for a failed launch: when the engine left a crash file, its path, why it stopped and its first frames, since "bridge closed" alone sent the reader to `fuactl crashes` to learn it had crashed at all.
export function crashedError(inst, e, _fs = fs) {
  const file = inst && inst.log ? `${inst.log}.crash` : null;
  if (!file || !_fs.existsSync(file)) return e;
  const { why, stack } = summarizeCrash(_fs.readFileSync(file, "utf8"));
  return new Error([`engine crashed -- ${file}`, why, ...stack].filter(Boolean).join("\n  "));
}

// [rc4l] body(inst), with the engine stopped if it throws: a launch that timed out kept its port, and every launch after it timed out too.
export async function stoppingOnFailure(inst, body, stop = stopInstance) {
  try {
    return await body(inst);
  } catch (e) {
    await stop(inst);
    throw crashedError(inst, e);
  }
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
    throw crashedError(inst, e);
  } finally {
    if (c) c.close();
    if (inst) await stop(inst);
  }
}

// [rc4l] A bridge connection that has said hello, or a throw when nothing answers on port.
async function bridgeConnect(port, token, { connectMs, helloMs } = {}) {
  const { BridgeClient } = await import("./client.mjs");
  const c = new BridgeClient();
  try { await c.connect(port, { token, timeoutMs: connectMs }); await c.waitHello(helloMs); return c; } catch (e) { c.close(); throw e; }
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
