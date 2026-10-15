// [rc4l] The crash files fuactl's launches leave beside their logs (mcp_crash.cpp), found and summed up, for a crash reported after the engine has gone.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

// [rc4l] A crash file's gist: why it stopped (the reason or the exception code) and its first frames, from the fault stack where it has one.
export function summarizeCrash(text, frames = 4) {
  const lines = String(text).split(/\r?\n/);
  const why = lines.find((l) => /^(reason|code):/.test(l)) || lines.find((l) => l.startsWith("===")) || "";
  const from = lines.findIndex((l) => l.startsWith("fault stack:"));
  const start = from >= 0 ? from : lines.findIndex((l) => l.startsWith("backtrace:"));
  const stack = start >= 0 ? lines.slice(start + 1).filter((l) => /^\s+#\d+/.test(l)).slice(0, frames).map((l) => l.trim()) : [];
  return { why: why.trim(), stack };
}

// [rc4l] The crash files under dir's fuactl folders changed since sinceMs, newest first.
export function recentCrashes(sinceMs, dir = os.tmpdir()) {
  const found = [];
  for (const d of fs.readdirSync(dir).filter((n) => n.startsWith("fuactl-"))) {
    const full = path.join(dir, d);
    let names = [];
    try { names = fs.readdirSync(full); } catch { continue; }
    for (const n of names.filter((x) => x.endsWith(".crash"))) {
      const file = path.join(full, n);
      const at = fs.statSync(file).mtimeMs;
      if (at >= sinceMs) found.push({ file, at });
    }
  }
  return found.sort((a, b) => b.at - a.at);
}
