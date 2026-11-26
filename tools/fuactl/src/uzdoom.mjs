// [Doruk-Aksoy] UZDoom side by side: run a pk3 in a UZDoom build, its window never activated on either backend, muted, on a config of its own, and read what it logged,
// so a port can be checked against upstream's behaviour as well as its code.
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { FREEDOOM2 } from "./template.mjs";
import { decodePNG, isBlankImage } from "./pngcore.mjs";
import { launchRenderer, launchBackendApi } from "./capture.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));

// [Doruk-Aksoy] The UZDoom to run: FUACTL_UZDOOM, else a Release build in a UZDoom-build folder beside the repository.
export function uzdoomExe(env = process.env) {
  return env.FUACTL_UZDOOM || path.resolve(HERE, "../../../../UZDoom-build/Release/uzdoom.exe");
}

// [Doruk-Aksoy] UZDoom's +logfile takes a bare name and writes log-<name>.txt in its working directory; a path makes it print "Could not start log".
export const LOG_NAME = "uzdoom";
// [Doruk-Aksoy] A release (4.14.3) writes the bare name itself, so whichever is there is the log, master's name when neither is.
export const logFileIn = (dir, exists = fs.existsSync) => {
  const master = path.join(dir, `log-${LOG_NAME}.txt`), release = path.join(dir, LOG_NAME);
  return !exists(master) && exists(release) ? release : master;
};

// [Doruk-Aksoy] UZDoom's backends by vid_preferbackend's value; Vulkan by default.
export const UZDOOM_BACKENDS = Object.freeze({ gl: 0, vulkan: 1 });

// [Doruk-Aksoy] The command line: the IWAD, the pk3, a config in `dir` so no one's settings change the run, muted, windowed, on `backend`, running though its window is
// never active (i_pauseinbackground stops the game otherwise, and vid_activeinbackground stops the drawing), then the map and one command chain.
// The chain is one string: UZDoom's `wait` only delays the rest of its own string, so "+wait 70 +quit" quits at once.
// The map is loaded a tic in, by that string, since a leading +map is a startup warp, after which UZDoom's OpenGL draws black, and takes the whole string as the map's name.
export function uzdoomCommandLine({ exe, iwad = FREEDOOM2, pk3, map, chain, dir, width = 1024, height = 640, backend = "vulkan", extra = [] }) {
  if (!(backend in UZDOOM_BACKENDS)) throw new Error(`no UZDoom backend ${backend} (${Object.keys(UZDOOM_BACKENDS).join(", ")})`);
  const q = (s) => `"${s}"`;
  return [q(exe), "-iwad", q(iwad), ...(pk3 ? ["-file", q(pk3)] : []), "-config", q(path.join(dir, "uzdoom.ini")), "-nosound", "-nomusic",
    "-width", width, "-height", height, "+vid_fullscreen", 0, "+vid_preferbackend", UZDOOM_BACKENDS[backend], "+queryiwad", 0, "+i_pauseinbackground", 0,
    "+vid_activeinbackground", 1,
    "+screenshot_dir", q(dir), "+screenshot_type", "png", "+logfile", LOG_NAME, ...extra, `+wait 1;map ${map};${chain}`].join(" ");
}

// [Doruk-Aksoy] Runs UZDoom on `map` of `pk3` with `chain`, in `dir`, and returns its log ("" when none), trying again when no log came, as UZDoom now and then stalls at start.
export function runUzdoom({ exe = uzdoomExe(), pk3, map = "MAP01", chain, dir, backend = "vulkan", extra = [], tries = 2, timeoutMs = 60000, run = spawnSync }) {
  if (!fs.existsSync(exe)) throw new Error(`no UZDoom at ${exe}; build it or set FUACTL_UZDOOM`);
  for (let attempt = 0; attempt < tries; attempt++) {
    const at = tries > 1 ? path.join(dir, `try${attempt + 1}`) : dir;
    fs.mkdirSync(at, { recursive: true });
    run("powershell", ["-NoProfile", "-File", path.join(HERE, "uzdoom-launch.ps1"), "-Exe", exe, "-Dir", at, "-TimeoutMs", String(timeoutMs)],
      { encoding: "utf8", env: { ...process.env, FUACTL_LAUNCH_CMDLINE: uzdoomCommandLine({ exe, pk3, map, chain, dir: at, backend, extra }) } });
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

// [Doruk-Aksoy] Whether UZDoom's log says it entered `map`: its "MAP01 - name" line, which a title screen or a map it could not find never prints.
export function uzdoomEntered(log, map) {
  return log.split(/\r?\n/).some((l) => l.toUpperCase().startsWith(`${map.toUpperCase()} - `));
}

// [Doruk-Aksoy] UZDoom's screenshot of `map` with `pk3` loaded, `wait` tics in, without the crosshair or weapon; its path, or an error saying what UZDoom logged, that it never entered the map, or that the picture is blank.
export function uzdoomScreenshot({ pk3, map, dir, wait = 70, extra = ["+crosshair", 0, "+r_drawplayersprites", 0], blank = (file) => isBlankImage(decodePNG(file)), ...rest }) {
  const log = runUzdoom({ pk3, map, dir, chain: `wait ${wait};screenshot;wait 35;quit`, extra, ...rest });
  const png = newestPng(dir);
  const tail = log.split("\n").slice(-8).join("\n");
  if (!png) throw new Error(`UZDoom gave no screenshot of ${map} in ${dir}:\n${tail}`);
  if (!uzdoomEntered(log, map)) throw new Error(`UZDoom never entered ${map}, so ${png} is not of it:\n${tail}`);
  if (blank(png)) throw new Error(`UZDoom's ${rest.backend || "vulkan"} picture of ${map} is blank (${png})`);
  return png;
}

// [Doruk-Aksoy] Our backend's APIs a parity harness runs against UZDoom: --api vulkan or opengl for one, both by default, as every test runs OpenGL beside Vulkan.
export function parityApis(args = []) {
  const i = args.indexOf("--api");
  const asked = i >= 0 ? args[i + 1] : "both";
  if (asked === "both") return ["vulkan", "opengl"];
  if (asked === "vulkan" || asked === "opengl") return [asked];
  throw new Error(`--api takes vulkan, opengl or both, not ${asked}`);
}

// [Doruk-Aksoy] The UZDoom backend a harness is judged against: --uzdoom-backend gl or vulkan, OpenGL by default, the reference parity is judged against first (UZDOOM_BACKENDS).
export function parityUzdoomBackend(args = []) {
  const i = args.indexOf("--uzdoom-backend");
  const asked = i >= 0 ? args[i + 1] : "gl";
  if (!(asked in UZDOOM_BACKENDS)) throw new Error(`--uzdoom-backend takes ${Object.keys(UZDOOM_BACKENDS).join(" or ")}, not ${asked}`);
  return asked;
}

// [Doruk-Aksoy] What `dumprenderer` must name for our launch on each API a harness asks for.
export const OUR_API_NAMES = Object.freeze({ vulkan: "Vulkan", opengl: "OpenGL" });

// [Doruk-Aksoy] Why our launch is not on `api` by its `dumprenderer` answer (a build without the OpenGL device ignores -opengl and runs Vulkan), or null when it is.
export function ourApiFault(api, probe) {
  const got = launchRenderer(probe) === "vulkan" ? launchBackendApi(probe) : null;
  return got === OUR_API_NAMES[api] ? null : `our launch asked for ${api} and runs ${got || launchRenderer(probe) || "no renderer"}; does this build have the ${api} device?`;
}
