// Launch a bridge-enabled engine instance under fuactl's supervision.
import { spawn } from "node:child_process";
import net from "node:net";
import crypto from "node:crypto";
import path from "node:path";
import fs from "node:fs";
import os from "node:os";

// Where each platform's compile script leaves a staged binary, relative to the repo root. The
// folder is the data dir too: it holds the pk3s and iwads/, so a launched engine finds its game
// data and `screenshot` writes the PNG there.
const STAGED_ENGINES = [
  "build/ForkUnderA.app/Contents/MacOS/forkundera",	// mac_compile.sh
  "dist-windows/forkundera.exe",				// windows_build_run.ps1
  "dist-linux/forkundera",					// linux_compile.sh
];

// Resolve the engine binary + its data dir. Override with FUACTL_ENGINE.
//
// [rc4l] All three platforms, not just the .app. This listed only the mac path, so on Windows every
// command that needs the binary failed with "engine binary not found" -- including `ui screenshot`,
// which is talking to an ALREADY RUNNING instance over a port and wants the path only to know where
// the PNG lands. `ui read` and every `rpc` worked, so the tool looked fine right up until the one
// call that reads a file back off disk. Found driving a Windows build.
export function resolveEngine() {
  const env = process.env.FUACTL_ENGINE;
  // cwd is either the repo root or tools/fuactl, so each candidate is tried from both.
  const candidates = env ? [env] : STAGED_ENGINES.flatMap((rel) => [
    path.resolve(process.cwd(), rel),
    path.resolve(process.cwd(), "../..", rel),
  ]);
  for (const c of candidates) if (fs.existsSync(c)) return c;
  throw new Error(`engine binary not found (set FUACTL_ENGINE). tried:\n  ${candidates.join("\n  ")}`);
}

export function freePort() {
  return new Promise((resolve, reject) => {
    const srv = net.createServer();
    srv.once("error", reject);
    srv.listen(0, "127.0.0.1", () => {
      const p = srv.address().port;
      srv.close(() => resolve(p));
    });
  });
}

// The engine command line for one instance, given the per-instance config path. Split out from the
// spawn so the argument list is testable without starting a process -- the cvars here decide what a
// harness run can and cannot do, and one wrong value is invisible until a drive silently no-ops.
// [rc4l] What size this instance renders at, in ONE place.
//
// A harness capture is only comparable to what someone reports if it frames the same thing, and
// framing is aspect ratio: at an identical camera a 4:3 window and a 16:10 one show different
// slices of the world. Reproducing a report at 640x480 against a 1280x800 window therefore looks
// like the wrong position even when x, y, z, yaw and pitch match to three decimals -- which cost
// several rounds of "that is not where I was standing", and it was the tool, not the position.
//
// opts.width/height override; otherwise a hands-on instance gets a window worth playing in and a
// locked one gets a quarter of it -- the SAME SHAPE, at half the width in each direction.
//
// The shape is the part that matters and the part that was wrong. A locked instance rendered
// 640x480 while a hands-on one rendered 1280x800: 4:3 against 16:10. At an identical camera those
// two show different amounts of the world sideways, so a capture reproducing a report framed a
// visibly different scene -- and the conclusion drawn from that, repeatedly, was that the POSITION
// was wrong. It was the aspect ratio.
//
// 1024x640 rather than a smaller 16:10 size because the engine SNAPS to a mode it knows: asking
// for 640x400 gets 640x480 back, silently, and the ini then says one thing while the captures say
// another. Verified by reading the size out of the PNG, which is the only claim worth believing.
export function resolutionOf(opts = {}) {
  return {
    w: Number(opts.width) || (opts.allowOsInput ? 1280 : 1024),
    h: Number(opts.height) || (opts.allowOsInput ? 800 : 640),
  };
}

// [rc4l] Seed the per-instance ini, because +set is too late for the video mode.
//
// vid_defwidth and vid_defheight are CVAR_GLOBALCONFIG: the engine reads them out of the ini while
// initialising video, which happens BEFORE the +set commands on the command line are executed. So
// passing them as +set silently did nothing -- the harness asked for 640x400 for months and always
// got 640x480, the engine default, and nobody noticed because both are small and 4:3.
export function writeInstanceIni(configPath, opts = {}) {
  const { w, h } = resolutionOf(opts);
  const body = [
    "[GlobalSettings]",
    `vid_defwidth=${w}`,
    `vid_defheight=${h}`,
    "fullscreen=false",
    "",
  ].join("\n");
  fs.writeFileSync(configPath, body, "utf8");
  return { w, h };
}

export function engineArgs(opts = {}, configPath) {
  const args = ["-iwad", opts.iwad || "freedoom2.wad"];
  // Isolated config, per instance. Without -config the engine reads AND writes the user's shared
  // ini, so a bridge run would both inherit whatever cvars the user happens to have archived (not a
  // clean, reproducible baseline) and, on exit, save its own overrides -- e.g. use_mouse 0 -- back
  // into that ini and break the user's real mouse. A fresh file starts from engine defaults, which is
  // identical across instances and never touches the user's config.
  args.push("-config", configPath);
  args.push(
    "-skill", String(opts.skill ?? 3),
    "+set", "fullscreen", "0",
    // [rc4l] A hands-on instance gets a window worth looking at. 640x400 is the harness size because
    // it makes screenshot pairs cheap to diff, and it is miserable to actually play in.
    // [rc4l] Still passed, but the ini below is what actually decides it -- see writeInstanceIni.
    "+set", "vid_defwidth", String(resolutionOf(opts).w),
    "+set", "vid_defheight", String(resolutionOf(opts).h),
    // Bridge instances are driven by the harness, never by a human at the window. Disable OS mouse
    // and joystick so a stray cursor drifting over one instance's window can't turn its view (a live
    // level grabs the mouse) and silently diverge a deterministic run from its twin. All synthetic
    // input still arrives through the bridge (input.event / input.look / input.axis), untouched.
    //
    // [rc4l] ...unless the human IS the driver. use_mouse 0 would leave --play with a keyboard-only
    // instance, which is not what "let me run around and show you" means.
    "+set", "use_mouse", opts.allowOsInput ? "1" : "0",
    "+set", "use_joystick", "0",
    // [rc4l] m_use_mouse is deliberately LEFT ALONE. It used to be forced to 0 here to silence the
    // menu cursor, but menu.cpp:859 drops every GUI mouse event when it is 0 -- including the ones
    // the harness injects. So `ui click`, `ui rightclick` and `ui drag` returned {"clicked":[x,y]}
    // and did nothing at all, on every platform. A harness must not disable the input it then
    // injects. Keeping a stray physical cursor out is the input lock's job
    // (ZANDRONUM_BRIDGE_INPUT_LOCK), not a cvar's.
  );
  if (opts.seed != null) args.push("-rngseed", String(opts.seed));
  // [rc4l] Arbitrary cvars, applied BEFORE the map loads. Some only take effect at level start --
  // sv_nomonsters is the obvious one -- so setting them over the bridge afterwards is too late and
  // the instance comes up full of monsters shooting at whatever is being measured.
  if (opts.cvars) {
    for (const [k, v] of Object.entries(opts.cvars)) args.push("+set", k, String(v));
  }
  if (opts.extraArgs) args.push(...opts.extraArgs);

  // [rc4l] The map goes LAST, because the engine runs `+` commands in the order they appear.
  //
  // This used to sit above the cvars, directly under a comment promising they were applied first.
  // They were not: +map ran, the level spawned, and only then did sv_nomonsters arrive -- so every
  // instance came up full of monsters despite being asked for none, including the hands-on build.
  // Monsters shove the player off a recorded position and shoot whatever is being measured, which
  // is the one thing a measurement instance must not have.
  if (opts.connect) args.push("+connect", opts.connect);
  else args.push("+map", opts.map || "MAP01");

  // [rc4l] ...and anything that only means something once a level EXISTS goes after it.
  //
  // Cheats are the case: `god` acts on a player pawn, so issued before the map it is either
  // ignored or applied to nothing. That is the mirror of the cvar bug above -- same command line,
  // opposite ordering requirement -- so both sides are stated here rather than left to whoever
  // adds the next argument.
  for (const c of opts.postMap || []) args.push(`+${c}`);

  return args;
}

// Spawn one instance. opts: { iwad, map, skill, seed, port, token, logDir, extraArgs, host }.
export async function launchInstance(opts = {}) {
  const bin = opts.engine || resolveEngine();
  const dir = path.dirname(bin);
  const port = opts.port || (await freePort());
  const token = opts.token || crypto.randomBytes(12).toString("hex");
  const logDir = opts.logDir || fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-"));
  const log = path.join(logDir, `engine-${port}.log`);
  const iniPath = path.join(logDir, "fua-bridge.ini");
  // Written BEFORE the engine starts, or the video mode is already chosen by the time it is read.
  writeInstanceIni(iniPath, opts);
  const args = engineArgs(opts, iniPath);

  const proc = spawn(bin, args, {
    cwd: dir,
    stdio: "ignore",
    env: {
      ...process.env,
      ZANDRONUM_BRIDGE_PORT: String(port),
      ZANDRONUM_BRIDGE_PARENT_PID: String(process.pid), // watchdog reaps if fuactl dies
      ZANDRONUM_BRIDGE_TOKEN: token,
      ZANDRONUM_BRIDGE_LOG: log,
      // Hands-off: the window drops OS keyboard/mouse; only harness-injected input reaches the sim.
      ZANDRONUM_BRIDGE_INPUT_LOCK: opts.allowOsInput ? "" : "1",
    },
  });
  return { proc, pid: proc.pid, port, token, host: opts.host || "127.0.0.1", log };
}

// Ask an instance to quit cleanly (SIGTERM -> engine's clean-quit path), then hard-kill after grace.
export function stopInstance(inst, graceMs = 4000) {
  return new Promise((resolve) => {
    if (!inst.proc || inst.proc.exitCode != null || inst.proc.signalCode) return resolve("already-exited");
    let done = false;
    const finish = (how) => { if (!done) { done = true; resolve(how); } };
    inst.proc.once("exit", () => finish("clean"));
    try { inst.proc.kill("SIGTERM"); } catch { /* ignore */ }
    setTimeout(() => { try { inst.proc.kill("SIGKILL"); } catch {} finish("forced"); }, graceMs);
  });
}
