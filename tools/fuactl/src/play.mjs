// [rc4l] A build you can actually walk around in, with the Vulkan view live in the window.
//
// One window. The backend renders into a disabled child covering the engine's client area, so the
// pixels on screen are Vulkan while the mouse and keyboard reach the engine exactly as always. GL
// still draws underneath, unseen -- that is what keeps the wall cache fed as you walk.
//
// This was a shell script for a long time, which meant the one launch path a human uses was the one
// path with no tests. The argument-ordering bug that shipped a "no monsters" build full of monsters
// lived here, under a comment claiming the opposite.
import fs from "node:fs";
import path from "node:path";
import { launchInstance, stopInstance } from "./launch.mjs";

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
export function playCvars({ gl = false, sideBySide = false, rt = false, monsters = false } = {}) {
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
  if (sideBySide) cv.fua_dg_embed = 0;
  // Ray-traced reflections must be on at LAUNCH: the acceleration structure is built at scene
  // upload, which has already happened by the time a level is playable, so setting it later leaves
  // nothing for the rays to hit.
  if (rt) cv.fua_dg_rtmirrors = 1;
  // Being shot at halfway through showing someone a texture seam is pure friction. Monsters go back
  // when the thing to look at IS a monster sprite.
  if (!monsters) cv.sv_nomonsters = 1;
  return cv;
}

// [rc4l] Resolve a catalogue entry to its file list, so playing what the HOST tab offers does not
// mean copying six pk3 paths out of a JSON file by hand. A missing file is NAMED rather than
// quietly dropped: a wad list that is one short loads a map with no textures and looks like a
// renderer bug.
export function resolvePreset(catalogueDir, storeDir, id, variantId) {
  const meta = JSON.parse(fs.readFileSync(path.join(catalogueDir, id, "addon.json"), "utf8"));
  const variant = variantId
    ? meta.variants.find((v) => v.id === variantId)
    : meta.variants.find((v) => v.default) || meta.variants[0];
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
    if (inst.proc.exitCode != null) throw new Error(`engine exited (${inst.proc.exitCode}) before reaching a level`);
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

export async function play(opts = {}) {
  const port = opts.port || 7900;
  const files = opts.preset
    ? resolvePreset(opts.catalogueDir, opts.storeDir, opts.preset, opts.variant)
    : (opts.file ? String(opts.file).split(",").map((s) => s.trim()) : []);

  const inst = await launchInstance({
    port,
    map: opts.map || "MAP01",
    iwad: opts.iwad || "doom2.wad",
    // [rc4l] Hands-on unless told otherwise -- but --lock exists because MY OWN launches must use
    // it. A hands-on instance takes the foreground and reads the mouse, so a stray movement while a
    // capture is running both perturbs the measurement and yanks the user's cursor into a game they
    // did not ask to be playing.
    allowOsInput: !opts.lock,
    // [rc4l] --res, forwarded. Without this it was accepted and silently dropped.
    width: opts.width, height: opts.height,
    extraArgs: files.flatMap((f) => ["-file", f]),
    cvars: playCvars(opts),
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
  } finally { c.close(); }
  inst.world = world;

  if (opts.sessionFile) {
    fs.writeFileSync(opts.sessionFile, `PORT=${inst.port}\nTOKEN=${inst.token}\n`);
  }
  return inst;
}

export { stopInstance };
