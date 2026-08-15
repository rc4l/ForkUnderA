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

// Spawn one instance. opts: { iwad, map, skill, seed, port, token, logDir, extraArgs, host }.
export async function launchInstance(opts = {}) {
  const bin = opts.engine || resolveEngine();
  const dir = path.dirname(bin);
  const port = opts.port || (await freePort());
  const token = opts.token || crypto.randomBytes(12).toString("hex");
  const logDir = opts.logDir || fs.mkdtempSync(path.join(os.tmpdir(), "fuactl-"));
  const log = path.join(logDir, `engine-${port}.log`);

  const args = ["-iwad", opts.iwad || "freedoom2.wad"];
  // Isolated config, per instance. Without -config the engine reads AND writes the user's shared
  // ini, so a bridge run would both inherit whatever cvars the user happens to have archived (not a
  // clean, reproducible baseline) and, on exit, save its own overrides -- e.g. use_mouse 0 -- back
  // into that ini and break the user's real mouse. A fresh file starts from engine defaults, which is
  // identical across instances and never touches the user's config.
  args.push("-config", path.join(logDir, "fua-bridge.ini"));
  // A client joins a server with +connect (and no local map); otherwise start in a local map.
  if (opts.connect) args.push("+connect", opts.connect);
  else args.push("+map", opts.map || "MAP01");
  args.push(
    "-skill", String(opts.skill ?? 3),
    "+set", "fullscreen", "0",
    "+set", "vid_defwidth", "640",
    "+set", "vid_defheight", "400",
    // Bridge instances are driven by the harness, never by a human at the window. Disable OS mouse
    // and joystick so a stray cursor drifting over one instance's window can't turn its view (a live
    // level grabs the mouse) and silently diverge a deterministic run from its twin. All synthetic
    // input still arrives through the bridge (input.event / input.look / input.axis), untouched.
    "+set", "use_mouse", "0",
    "+set", "use_joystick", "0",
    "+set", "m_use_mouse", "0", // also silence the MENU cursor, so nothing the harness didn't inject moves it
  );
  if (opts.seed != null) args.push("-rngseed", String(opts.seed));
  if (opts.extraArgs) args.push(...opts.extraArgs);

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
