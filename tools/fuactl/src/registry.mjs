// The instance registry: ~/.forkundera/instances/<pid>.json, self-registered by every bridge-enabled
// engine. The reaper reads it, cleanly signals live instances, and prunes dead entries -- so orphans
// never accumulate regardless of how the engine was launched.
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import { partitionRegistry } from "./proto.mjs";

export function registryDir() {
  return path.join(os.homedir(), ".forkundera", "instances");
}

export function readRegistry() {
  const dir = registryDir();
  let files;
  try { files = fs.readdirSync(dir); } catch { return []; }
  const out = [];
  for (const f of files) {
    if (!f.endsWith(".json")) continue;
    const full = path.join(dir, f);
    try {
      const e = JSON.parse(fs.readFileSync(full, "utf8"));
      out.push({ ...e, _file: full });
    } catch {
      out.push({ pid: NaN, _file: full }); // unreadable -> treated as stale
    }
  }
  return out;
}

export function pidAlive(pid) {
  if (!Number.isInteger(pid)) return false;
  try { process.kill(pid, 0); return true; }
  catch (e) { return e.code === "EPERM"; } // EPERM => alive but not ours; ESRCH => gone
}

// Reap: SIGTERM live instances (clean quit) when kill=true, and delete every stale entry's file.
export function reap({ kill = false, signal = "SIGTERM" } = {}) {
  const entries = readRegistry();
  const { live, stale } = partitionRegistry(entries, pidAlive);
  const killed = [];
  if (kill) {
    for (const e of live) {
      try { process.kill(e.pid, signal); killed.push(e.pid); } catch { /* ignore */ }
    }
  }
  const pruned = [];
  for (const e of stale) {
    try { fs.unlinkSync(e._file); pruned.push(e._file); } catch { /* ignore */ }
  }
  return { live, stale, killed, prunedCount: pruned.length };
}
