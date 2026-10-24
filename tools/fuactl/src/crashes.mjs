// [rc4l] The crash files fuactl's launches leave beside their logs (mcp_crash.cpp), found and summed up, for a crash reported after the engine has gone.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

// [rc4l] A crash file's gist: why it stopped (the reason or the exception code), the seam each thread was in, and its first frames, from the fault stack where it has one.
export function summarizeCrash(text, frames = 4) {
  const lines = String(text).split(/\r?\n/);
  const why = lines.find((l) => /^(reason|code|signal):/.test(l)) || lines.find((l) => l.startsWith("===")) || "";
  const seams = (lines.find((l) => l.startsWith("seams:")) || "").replace(/^seams:\s*/, "");
  const from = lines.findIndex((l) => l.startsWith("fault stack:"));
  const start = from >= 0 ? from : lines.findIndex((l) => l.startsWith("backtrace:"));
  // [rc4l] A frame is Windows' "  #3 Func + 0x12" or the POSIX backtrace_symbols line "3   forkundera   0x0001 _ZFunc + 280".
  const stack = start >= 0 ? lines.slice(start + 1).filter((l) => /^\s+#\d+|^\d+\s+\S+\s+0x[0-9a-f]+/i.test(l)).slice(0, frames).map((l) => l.trim()) : [];
  return { why: why.trim(), seams, stack };
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
