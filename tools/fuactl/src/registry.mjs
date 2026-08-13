// The instance registry: ~/.forkundera/instances/<pid>.json, self-registered by every bridge-enabled
// engine. The reaper reads it, cleanly signals live instances, and prunes dead entries -- so orphans
// never accumulate regardless of how the engine was launched.
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import { classifyInstances } from "./proto.mjs";

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

// Reap, MULTI-SESSION SAFE. Always prunes dead entries' files. With kill=true, SIGTERMs only genuine
// ORPHANS (engine alive but its launcher gone) -- so it never touches an engine another concurrent
// session/folder is actively using. Pass all=true to also stop live, still-owned instances (the
// explicit "kill everything on this machine" escape hatch, like the MCP reset's all:true).
export function reap({ kill = false, all = false, signal = "SIGTERM" } = {}) {
  const entries = readRegistry();
  const { dead, orphan, owned } = classifyInstances(entries, pidAlive);
  const killed = [];
  if (kill || all) {
    const targets = all ? [...orphan, ...owned] : orphan;
    for (const e of targets) {
      try { process.kill(e.pid, signal); killed.push(e.pid); } catch { /* ignore */ }
    }
  }
  const pruned = [];
  for (const e of dead) {
    try { if (e && e._file) fs.unlinkSync(e._file); pruned.push(e && e._file); } catch { /* ignore */ }
  }
  return { orphan, owned, dead, killed, prunedCount: pruned.length };
}
