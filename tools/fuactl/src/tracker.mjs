// [rc4l] `fuactl tracker` -- the commit trackers' walk (commit-tracker/tracker.py), run with a real Python and Mercurial found for it.
import { spawnSync } from "node:child_process";
import path from "node:path";

// [rc4l] Interpreters to try in turn: $PYTHON first, then the usual names; `py -3` is the Windows launcher.
export function pythonCandidates(env = process.env, platform = process.platform) {
  const out = [];
  if (env.PYTHON) out.push([env.PYTHON]);
  out.push(["python3"], ["python"]);
  if (platform === "win32") out.push(["py", "-3"]);
  return out;
}

// [rc4l] Windows' python3 is often the Microsoft Store stub, which prints nothing and exits non-zero, so a candidate counts only if it runs Python 3.7 or later.
export function findPython({ env = process.env, platform = process.platform, run = spawnSync } = {}) {
  for (const [cmd, ...pre] of pythonCandidates(env, platform)) {
    const r = run(cmd, [...pre, "-c", "import sys; sys.exit(sys.version_info < (3, 7))"], { stdio: "ignore" });
    if (r && r.status === 0) return [cmd, ...pre];
  }
  return null;
}

// [rc4l] Mercurial for the Zandronum ledger: $HG, else hg on PATH; null when neither runs.
export function findHg({ env = process.env, run = spawnSync } = {}) {
  for (const cmd of [env.HG, "hg"].filter(Boolean)) {
    const r = run(cmd, ["--version", "--quiet"], { stdio: "ignore" });
    if (r && r.status === 0) return cmd;
  }
  return null;
}

export const TRACKER_USAGE = `usage: fuactl tracker <command> [args]   (commit-tracker/tracker.py; --help on any command)
  next [--ledger L]                          the first pending row: files in our tree, twin, left-out hunks, tags, presence hint, diffstat
  show SHA [--ledger L] [--json]             the same for any row
  diff SHA [--ledger L] [--apply]            its diff minus ignored paths; --apply lands it under src/zandronum (zero .rej or it fails)
  mark SHA STATUS [--ours S,..] [--note T] [--append]   set a verdict, the twin's with it, and progress.json
  pull [--ledger L]                          fetch upstream, regen, tags, progress, check
  check [--ledger L]                         what CI enforces for every ledger
  regen | progress [--ledger L]              rebuild a ledger from its clone / recompute its progress bar
ledgers: uzdoom (clone ../UZDoom or $UZDOOM), zandronum (clone ../Zandronum or $ZANDRONUM; needs Mercurial: hg on PATH or $HG)`;

// [rc4l] The command line tracker.py gets: the script, then the arguments as given.
export function trackerArgs(root, python, argv) {
  return [...python.slice(1), path.join(root, "commit-tracker", "tracker.py"), ...argv];
}

// [rc4l] Runs tracker.py in the foreground and returns its exit status; 2 when no Python is found.
export function runTracker(root, argv, { env = process.env, run = spawnSync, log = console.error } = {}) {
  if (!argv.length || argv[0] === "help" || argv[0] === "--help") { log(TRACKER_USAGE); return argv.length ? 0 : 2; }
  const python = findPython({ env, run });
  if (!python) { log("fuactl tracker: no Python 3 found (set PYTHON to one)"); return 2; }
  const hg = findHg({ env, run });
  const childEnv = hg ? { ...env, HG: hg } : env;
  const r = run(python[0], trackerArgs(root, python, argv), { stdio: "inherit", env: childEnv, cwd: root });
  return r && typeof r.status === "number" ? r.status : 1;
}
