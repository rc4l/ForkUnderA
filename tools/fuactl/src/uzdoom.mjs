// [Doruk-Aksoy] UZDoom side by side: run a pk3 in a UZDoom build, its window never activated, muted, on a config of its own, and read what it logged,
// so a port can be checked against upstream's behaviour as well as its code.
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { FREEDOOM2 } from "./template.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));

// [Doruk-Aksoy] The UZDoom to run: FUACTL_UZDOOM, else a Release build in a UZDoom-build folder beside the repository.
export function uzdoomExe(env = process.env) {
  return env.FUACTL_UZDOOM || path.resolve(HERE, "../../../../UZDoom-build/Release/uzdoom.exe");
}

// [Doruk-Aksoy] UZDoom's +logfile takes a bare name and writes log-<name>.txt in its working directory; a path makes it print "Could not start log".
export const LOG_NAME = "uzdoom";
export const logFileIn = (dir) => path.join(dir, `log-${LOG_NAME}.txt`);

// [Doruk-Aksoy] The command line: the IWAD, the pk3, a config in `dir` so no one's settings change the run, muted, windowed, on Vulkan, running though its window is
// never active (i_pauseinbackground stops the game otherwise, so a run stalls whenever Windows calls it inactive), then the map and one command chain.
// The chain is one string: UZDoom's `wait` only delays the rest of its own string, so "+wait 70 +quit" quits at once.
export function uzdoomCommandLine({ exe, iwad = FREEDOOM2, pk3, map, chain, dir, width = 1024, height = 640, extra = [] }) {
  const q = (s) => `"${s}"`;
  return [q(exe), "-iwad", q(iwad), ...(pk3 ? ["-file", q(pk3)] : []), "-config", q(path.join(dir, "uzdoom.ini")), "-nosound", "-nomusic",
    "-width", width, "-height", height, "+vid_fullscreen", 0, "+vid_preferbackend", 1, "+queryiwad", 0, "+i_pauseinbackground", 0,
    "+screenshot_dir", q(dir), "+screenshot_type", "png", "+logfile", LOG_NAME, ...extra, "+map", map, `+${chain}`].join(" ");
}

// [Doruk-Aksoy] Runs UZDoom on `map` of `pk3` with `chain`, in `dir`, and returns its log ("" when none), trying again when no log came, as UZDoom now and then stalls at start.
export function runUzdoom({ exe = uzdoomExe(), pk3, map = "MAP01", chain, dir, extra = [], tries = 2, timeoutMs = 60000, run = spawnSync }) {
  if (!fs.existsSync(exe)) throw new Error(`no UZDoom at ${exe}; build it or set FUACTL_UZDOOM`);
  for (let attempt = 0; attempt < tries; attempt++) {
    const at = tries > 1 ? path.join(dir, `try${attempt + 1}`) : dir;
    fs.mkdirSync(at, { recursive: true });
    run("powershell", ["-NoProfile", "-File", path.join(HERE, "uzdoom-launch.ps1"), "-Exe", exe, "-Dir", at, "-TimeoutMs", String(timeoutMs)],
      { encoding: "utf8", env: { ...process.env, FUACTL_LAUNCH_CMDLINE: uzdoomCommandLine({ exe, pk3, map, chain, dir: at, extra }) } });
    const log = logFileIn(at);
    if (fs.existsSync(log)) return fs.readFileSync(log, "utf8");
  }
  return "";
}

// [Doruk-Aksoy] The newest PNG anywhere under `dir`, or null.
export function newestPng(dir) {
  let best = null;
  for (const entry of fs.readdirSync(dir, { withFileTypes: true, recursive: true })) {
    if (!entry.isFile() || !entry.name.endsWith(".png")) continue;
    const file = path.join(entry.parentPath ?? entry.path, entry.name);
    const at = fs.statSync(file).mtimeMs;
    if (!best || at > best.at) best = { file, at };
  }
  return best ? best.file : null;
}

// [Doruk-Aksoy] UZDoom's screenshot of `map` with `pk3` loaded, `wait` tics in, without the crosshair or weapon; its path, or an error saying what UZDoom logged.
export function uzdoomScreenshot({ pk3, map, dir, wait = 70, extra = ["+crosshair", 0, "+r_drawplayersprites", 0], ...rest }) {
  const log = runUzdoom({ pk3, map, dir, chain: `wait ${wait};screenshot;wait 35;quit`, extra, ...rest });
  const png = newestPng(dir);
  if (!png) throw new Error(`UZDoom gave no screenshot of ${map} in ${dir}:\n${log.split("\n").slice(-8).join("\n")}`);
  return png;
}
