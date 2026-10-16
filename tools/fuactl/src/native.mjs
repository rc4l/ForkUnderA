// [rc4l] fuactl's native helpers (native/<name>/<name>.cpp, Windows only), each built with cmake on first use and again when its source is newer.
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const ROOT = fileURLToPath(new URL("../native/", import.meta.url));

// [rc4l] Where a helper's source, build tree and executable live.
export function nativePaths(name, root = ROOT) {
  const dir = path.join(root, name);
  return { source: path.join(dir, `${name}.cpp`), build: path.join(dir, "build"), exe: path.join(dir, "build", "Release", `${name}.exe`) };
}

// [rc4l] Whether a helper must be built again: it is missing, or older than its source.
export function needsBuild(paths, stat = (p) => (fs.existsSync(p) ? fs.statSync(p).mtimeMs : null)) {
  const exe = stat(paths.exe);
  return exe === null || exe < stat(paths.source);
}

// [rc4l] Build the named helper with cmake when it needs it, and return its path.
export function ensureNative(name, cmake, { paths = nativePaths(name), run = spawnSync, log = () => {} } = {}) {
  if (!needsBuild(paths)) return paths.exe;
  log(`building the ${name} helper`);
  const dir = path.dirname(paths.source);
  for (const args of [["-S", dir, "-B", paths.build], ["--build", paths.build, "--config", "Release"]]) {
    const r = run(cmake, args, { encoding: "utf8" });
    if (r.status !== 0) throw new Error(`${name} build failed: ${((r.stdout || "") + (r.stderr || "")).split(/\r?\n/).filter((l) => /error/i.test(l)).slice(0, 5).join(" | ")}`);
  }
  return paths.exe;
}
