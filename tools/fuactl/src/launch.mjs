// Launch a bridge-enabled engine instance under fuactl's supervision.
import { spawn } from "node:child_process";
import net from "node:net";
import crypto from "node:crypto";
import path from "node:path";
import fs from "node:fs";
import os from "node:os";

// Resolve the engine binary + its data dir. Override with FUACTL_ENGINE (path to the `forkundera`
// binary inside the .app, whose folder also holds the IWAD + pk3s).
export function resolveEngine() {
  const env = process.env.FUACTL_ENGINE;
  const candidates = env ? [env] : [
    path.resolve(process.cwd(), "build/ForkUnderA.app/Contents/MacOS/forkundera"),
    path.resolve(process.cwd(), "../../build/ForkUnderA.app/Contents/MacOS/forkundera"),
  ];
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

  const args = [
    "-iwad", opts.iwad || "freedoom2.wad",
    "+map", opts.map || "MAP01",
    "-skill", String(opts.skill ?? 3),
    "+set", "fullscreen", "0",
    "+set", "vid_defwidth", "640",
    "+set", "vid_defheight", "400",
  ];
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
