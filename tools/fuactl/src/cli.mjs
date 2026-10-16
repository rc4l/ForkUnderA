#!/usr/bin/env node
// fuactl -- ForkUnderA companion. One tool, two faces:
//   fuactl <cmd>     humans / CI          fuactl mcp     agents (MCP stdio server)
// Both talk to the engine's native bridge (features/mcp-bridge), which must be built with
// -DFUA_MCP_BRIDGE=ON (ZX_MCP_BRIDGE=1 ./mac_compile.sh) and armed with ZANDRONUM_BRIDGE_PORT.
import { reap, readRegistry, pidAlive } from "./registry.mjs";
import { runDeterminismCheck, runPerfAblation, runNetBandwidth, runGlTimers } from "./session.mjs";
import { launchInstance, stopInstance, resolveEngine, engineProcessCount, resFlag, slashed } from "./launch.mjs";
import { BridgeClient } from "./client.mjs";
import { diligentRun } from "./diligent.mjs";
import { sampleProcess } from "./sample.mjs";
import { summarizeGlTimers, liveOnPort } from "./proto.mjs";
import * as ui from "./ui.mjs";
import { runBench } from "./bench.mjs";
import * as cap from "./capture.mjs";
import { play, quitInstance, defaultPwadStore, resolvePreset } from "./play.mjs";
import { loadQueue, saveQueue, nextId, spotNameFor, addItem, noteItem, closeItem, nextOpen, formatItem } from "./queue.mjs";
import { findMapLumps, mapReport } from "./mapdata.mjs";
import { engineWindows, resizeEngineWindow } from "./window.mjs";
import * as shot from "./shot.mjs";
import * as fixtures from "./fixtures.mjs";
import { sweepDirFor } from "./shot.mjs";
import * as sweepMod from "./sweep.mjs";
import { png } from "./png.mjs";
import { actorsNear } from "./actors.mjs";
import { pixelToFace } from "./camera.mjs";
import { sectorOf } from "./sectorof.mjs";
import { scenarioOptions, scenarioRun, templateScenarioFiles, probeScenario, sideAdjust, standaloneOptions, matrixSets, matrixAdjust, webgpuOptions, launchRenderer } from "./template.mjs";
import { abPerf } from "./abperf.mjs";
import { measureCosts, formatCosts } from "./costs.mjs";
import { runFps, fpsModes } from "./fps.mjs";
import * as screen from "./screen.mjs";
import { ensureNative } from "./native.mjs";
import { nativeStacks } from "./stacks.mjs";
import * as trace from "./trace.mjs";
import * as present from "./present.mjs";
import { refreshNote } from "./stats.mjs";
import { runCoverage } from "./coverage.mjs";
import { changeSince, commentIssues } from "./comments.mjs";
import { machinePatterns, patternsPath, privacyIssues } from "./privacy.mjs";
import { recentCrashes, summarizeCrash } from "./crashes.mjs";
import { describeSimTraceDiff, diffSimTraces, parseSimTrace } from "./simtrace.mjs";
import * as telemetry from "./telemetry.mjs";
import { decodePNG, abDiff, boxDiff } from "./pngcore.mjs";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { usageFor } from "./usage.mjs";
import { diagnoseHang, doctorEngine } from "./diagnose.mjs";

const num = (v) => (v != null && v !== true ? Number(v) : undefined);

const SPOTS = new URL("../spots.json", import.meta.url);
const SESSION = () => path.resolve(process.cwd(), ".play-session");
// [rc4l] The engine process of the instance on --port (else the last play), from its play session, which the bridge does not report.
const sessionPid = (flags) => {
  const pid = shot.readSession(shot.restartSessionFile(SESSION(), flags.port))?.pid;
  if (!pid) throw new Error(`no play session names the engine's process${flags.port ? ` on port ${flags.port}` : ""}`);
  return pid;
};

// [rc4l] The instance a command means: --port, else the last play session, else null.
function sessionTarget(flags) {
  const shared = SESSION();
  return shot.resolveTarget(flags, shot.readSession(flags.port ? shot.sessionFileFor(shared, flags.port) : shared));
}

// [rc4l] The binary a command's instance runs, which is where its captures land: --engine, its play session's, else the staged one.
function engineOf(flags) {
  return flags.engine || (sessionTarget(flags) || {}).engine || resolveEngine();
}

// [rc4l] sessionTarget, or exit with the command's usage when there is none.
function targetOf(flags, usage = "no --port and no .play-session -- nothing running") {
  const t = sessionTarget(flags);
  if (!t) { console.error(usage); process.exit(2); }
  return t;
}

// [rc4l] Save the instance's vantage as spot name in file (spots.json by default) with what was loaded (the engine's own `wads`, hashes included) and how the renderer was set, since a camera alone is not a repro; returns the camera.
async function captureSpot(c, name, note, file = SPOTS) {
  const cam = await c.rpc("player.camera");
  const wadText = await cap.exec(c, "wads", { quietMs: 400, maxMs: 8000 }).catch(() => "");
  const iwad = (wadText.match(/^IWAD:\s*(.+)$/m) || [])[1] || null;
  const pwads = [...wadText.matchAll(/^PWAD:\s*(\S+)(?:\s*-\s*([0-9a-f]{32}))?/gm)]
    .map((m) => (m[2] ? `${m[1]} ${m[2]}` : m[1]));
  const loaded = { iwad: iwad && iwad.trim(), pwads };
  const renderer = {};
  for (const cv of ["fua_vulkan", "fua_dg_standalone", "fua_dg_embed", "fua_dg_skyportal", "gl_wallmesh"]) {
    const v = shot.cvarValueFrom(await cap.exec(c, cv, { quietMs: 300, maxMs: 6000 }).catch(() => ""));
    if (v !== null) renderer[cv] = v;
  }
  // [rc4l] Whether the launch had GL at all, which no cvar says: "vulkan" for a -vulkan launch.
  const backend = launchRenderer(await cap.exec(c, "dumprenderer", { quietMs: 300, maxMs: 6000 }).catch(() => ""));
  if (backend) renderer.backend = backend;
  const db = fs.existsSync(file) ? JSON.parse(fs.readFileSync(file, "utf8")) : { spots: {} };
  db.spots[name] = Object.assign({}, db.spots[name], {
    map: cam.map, ...shot.atFromCamera(cam),
    shows: note || (db.spots[name] && db.spots[name].shows) || "recorded from a live session",
    loaded, renderer,
  });
  fs.writeFileSync(file, JSON.stringify(db, null, 2) + "\n");
  console.error(`saved spot ${name}`);
  return cam;
}

// [rc4l] Where --preset finds its catalogue entry and its wads: --store, then FUACTL_PWADS, then the in-engine downloader's store.
function presetDirs(flags) {
  return {
    catalogueDir: path.resolve(fileURLToPath(new URL(".", import.meta.url)), "../../../catalogue"),
    storeDir: flags.store || process.env.FUACTL_PWADS || defaultPwadStore(),
  };
}

async function connectTarget(flags, usage) {
  // [rc4l] Every command waits out a launch still checking its world on its port, so a poll started right after `play` cannot kill it.
  const port = flags.port || (sessionTarget(flags) || {}).port;
  if (port && await shot.awaitLaunch(SESSION(), port)) console.error(`waited for the launch on port ${port}`);
  const t = targetOf(flags, usage);
  const c = new BridgeClient();
  await c.connect(t.port, { token: t.token });
  await c.waitHello();
  return c;
}

// Connect to an instance, run fn with the client, print nothing extra, then close.
async function withUi(flags, fn) {
  const c = await connectTarget(flags, "ui needs --port P [--token T], or a running play session");
  try { return await fn(c); } finally { c.close(); }
}

function parseFlags(argv) {
  const flags = {}; const rest = [];
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a.startsWith("--")) {
      const key = a.slice(2);
      const val = (i + 1 < argv.length && !argv[i + 1].startsWith("--")) ? argv[++i] : true;
      // [rc4l] A repeated flag accumulates instead of overwriting.
      //
      // Silently keeping only the last one is a trap for exactly the flags you repeat: `--cvar
      // sv_nomonsters=1 --cvar fua_vulkan=1` launched with the renderer switch dropped on the floor
      // and no error anywhere, which reads as "the backend did not come up" rather than "the flag
      // never arrived". Values are joined with a comma, which is the separator --cvar and --file
      // already split on.
      flags[key] = (key in flags && typeof flags[key] === "string" && typeof val === "string")
        ? `${flags[key]},${val}`
        : val;
    } else rest.push(a);
  }
  return { flags, rest };
}

const USAGE = `fuactl <command>
  ls                                 list registered engine instances
  reap [--kill] [--all]              prune dead; --kill SIGTERMs ORPHANS only (other sessions safe); --all kills every live instance
  launch [--map M] [--seed S] [--port P] [--token T] [--iwad W] [--skill N] [--file a.wad,b.pk3] [--cvar k=v,k2=v2] [--arg "-host,+sv_hostname,X"] [--play]   launch one supervised bridge instance (stays up until Ctrl-C)
  sample --pid P | --port P [--seconds N] [--top N] [--engine] [--stacks]   hottest functions (macOS sample / Linux perf / Windows in-engine, which needs --port); --stacks adds each function's inclusive share on Windows
  hang --port P [--token T] [--pid P] [--gap MS]   why an instance stopped answering: gone / unreachable / stalled / paused / healthy, plus the function it is stuck in (on Windows from its busiest threads' native stacks)
  doctor [--exe PATH] [--src DIR]      is the configured engine driveable at all (bridge compiled in?) and is it older than the source?
  net-bw [--seed S] [--map M] [--spawn CLS] [--count N] [--seconds N]   client/server bandwidth, baseline vs perturbation
  cycle --port P [--token T] [--count N] [--settle MS]   advance the map rotation N times, confirming intermissions
  rpc <cmd> [jsonArgs] --port P [--token T]   send one RPC to an instance and print the result
  queue add <description> [--port P] [--name SPOT] [--no-instance [--map M]]   save the instance's vantage as a spot and queue the problem seen there (report-queue skill); --no-instance queues one seen where no engine answers (a browser page, an engine already gone), with its map when known and no spot
  queue list [--all] | next | note <id> <text> | done <id> [--unverified] [--note TEXT]   read and work the queue, oldest open item first
  burst <tag> [--side gl|vk] [--count N] [--box x0,y0,x1,y1] [--spot S | --at --face] [--port P]   timed captures with the sim running and each one's box brightness, for an animation's rate or a light a paused shot hides
  steps <tag> --hold +ACTION [--count N] [--spot S | --at --face] [--port P]   an action (+crouch) held and stepped one tic at a time, a GL/Vulkan pair and the eye height each tic, for a fault that shows only mid-transition
  sector-of --pixel x,y [--side gl|vk] [--spot S | --at --face] [--port P]   which sector's colour that pixel of a capture follows, found by painting halves of the map red and green (fua_sector_color); the sectors stay painted
  travel --to MAP [--max N] [--port P]   nextmap until the instance is on MAP, the way a player reached it
  quit [--port P] [--token T] [--park]   quit an instance (else the last play session) and wait until its port closes; one that refuses (no token) and whose launcher is gone is stopped as \`reap --kill\` would; exit 1 if it would not go; --park files its camera, wads and renderer first
  session [--instances N] [--seed S] [--map M] [--tics T]   run the determinism + desync check
  perf-ab [--seed S] [--map M] [--spawn CLS] [--count N] [--frames F]   deterministic perf ablation (baseline vs perturbation, causal ms delta + sim/render verdict)
  gl-timers --port P [--token T] [--frames N] [--warmup M]   GPU render profiling: per-pass GPU ms (scene/translucent/hud2d) of a running instance
  capture --port P [--frames N] [--warmup M]   frametime distribution (p50/p95/p99/max, sim vs render split)
  screen --port P [--token T] [--seconds N] [--orbit DEG] [--period S] [--class WINDOWCLASS] [--pid N]   what reaches the screen of a running instance (Windows): new images shown, refreshes repeated and stalls a second, from the compositor's own frames, with the aim orbiting so every refresh should bring a new image; with the engine's own frames lined up against them, how old each image's content was when it was seen and how much that changed (judder)
  fps --map M [--iwad W] [--file a.wad,...] [--modes gl,embed,vulkan] [--res WxH] [--rounds N] [--frames N] [--warmup N] [--vsync] [--sample SEC] [--no-shot] [--engine PATH] [--port P] [--cvar k=v] [--label L]   frame rate under GL alone, the backend in the GL launch and the backend alone, each a locked launch of its own, one at a time; notes a median that sits on a display refresh
  ticprof --port P [--tics N]          per-tic sim phase split (P_Ticker / thinkers / effects / specials)
  simtrace --port P [--token T] --out FILE [--tics N]   each tic's fingerprint and every damage, kill and spawn for the next N tics (sim.trace), into FILE; +fua_simtrace N FILE on a launch traces from its first tic
  tracediff A B   where two sim traces part: the first tic whose fingerprint differs and the first event that moved; exit 1 when they differ
  fixture record|trace|check|pictures|vischeck [NAME ...] [--engine PATH] [--store DIR] [--cvar k=v,...] [--update] [--out FILE] [--tolerance T]   the game-thread fixture set (fixtures.mjs: a generated map with every tricky ingredient, stock maps, Sunder MAP11): record each one's ticcmd demo, trace its playback tic by tic, hold each trace to its saved baseline, capture its views at a fixed tic and hold each to a saved PNG (largest 16-pixel block within T, default 2), or play it with GL beside the backend and every walk checked against GL's; the first check or pictures run saves the baseline, exit 1 on a difference; demos and baselines live in ~/.forkundera/fixtures
  bench --port P --scenario F.json [--runs N] [--metric total.p99_ms]   repeat a scenario, report median + spread, discard runs whose expectations failed
  lines --port P [--special N] [--door] [--use] [--cross] [--tag N] [--texture NAME] [--limit N]   query linedefs; prints a stand position and facing for each match
  here --port P [--token T] [--save NAME] [--note TEXT]   the live camera (position + facing, full precision) and what the crosshair is on; --save records it in spots.json as a named repro
  renderer-info --port P [--token T]   renderer identity + whether GL timer queries work on this driver
  ui <action> [args] --port P [--token T]   drive the UI: read (menu as text), find <label>, nav <keys>, click <x> <y>, drag, type <text>, look --yaw D --pitch D, screenshot [name], exec <ccmd>
  diligent --port P [--frames N] [--shot FILE] [--sweep DIR]   drive the Diligent (Vulkan) backend: bake the level mesh, upload geometry, optional swapchain screenshot, optional debug-view sweep (lm0..lm4.png in DIR), the Diligent frame benchmark (fua_diligent_bench), and with --scale a GPU-time probe at 1x..100x the visible geometry
  trace --port P [--token T] [--seconds N] [--out FILE.csv]   every frame of a running instance for N seconds (fua_viewtrace): frame lengths apart from tic waits, what the slow frames spent more on, where it sat paused or unfocused, and the mouse input it got, gaps included
  present [--seconds N] [--process NAME.exe] [--port P [--token T]] [--out FILE.csv]   what Windows put on screen, from PresentMon run as administrator (a UAC prompt): each app's frames handed over and shown a second and the compositor's own updates; with --port the instance's frames are recorded alongside and lined up second by second
  play [--restart] [--port P] [--token T] [--spot NAME | --unpark] [--map M] [--file a.pk3,b.pk3] [--cvar k=v,k2=v2] [--preset ID --variant V --store DIR] [--gl | --no-gl | --webgpu] [--side-by-side] [--rt] [--monsters] [--lock] [--res WxH]   a build to walk around in, Vulkan live in the window; stays up until Ctrl-C
  build [--root DIR] [--no-stage] [--tests]   compile the engine (with --tests the unit tests too, and run them) and stage it unless --no-stage, failing loudly instead of staging a stale binary
  crashes [--hours N]   the crash files fuactl's launches left in the last N hours (default 24), newest first, each with why it stopped and its first frames
  comments [--since REF]   the [rc4l] comments a change adds since REF (default HEAD, untracked files included) that run past one line or one sentence, as AGENTS.md has them; exit 1 on any
  privacy [--since REF | --staged]   what a change adds since REF (default HEAD, untracked files included), or what is staged, that could identify its author: a home folder's path, an email address, this machine's user or host name, or a pattern in ~/.forkundera/private-patterns.txt; exit 1 on any
  coverage [--since REF] [--tool PATH]   the 100% line-coverage gate on Windows: builds the Debug unit tests and runs them under OpenCppCoverage, then checks every unit a NAME_test.cpp gates, or with --since only those changed since REF (new files included); exit 1 on any miss
  mapdata <MAP> (--preset ID | --file a.pk3,b.wad) [--sector N,..] [--line N,..] [--special N] [--tag N] [--thing TYPE,..] [--scripts REGEX]   a UDMF level's summary, sectors, lines, things of the given types and ACS source lines, read from its files
  actors [--near x,y] [--radius R] [--toward YAW | --ahead] [--cone DEG] [--class REGEX]   actors near a point (default the player), nearest first, optionally only in a direction
  probe <scenario.mjs> [--map M] [--vantage V] [--side S] [--engine path/to/forkundera.exe] [<command> ...]   launch a template scenario's map at a vantage and run console commands there
  scenarios [name ...] [--engine path/to/forkundera.exe] [--side S --cvar k=v,...] [--standalone | --webgpu] [--matrix k=a,b]   run every template scenario (or the named ones) in turn and summarise; --engine picks the binary, as FUACTL_ENGINE does; --side/--cvar adds cvars to one side, to show a test fails under the old behaviour (scenarios without that side are skipped); --standalone runs each as players do: its own standalone side, else its vk side with fua_dg_standalone 1, else skipped; --webgpu runs each one's Vulkan side on WebGPU against Vulkan, every box at WEBGPU_CEILING; --matrix runs the suite once per value with the cvar on every side (fua_gamethread=0,1)
  ready [--port P --token T] [--timeout S]   wait until an instance answers (defaults to the last play session)
  goto (--spot NAME | --at x,y,z --face yaw,pitch) [--port P]   stand the player there
  motion [--spot NAME | --at x,y,z --face yaw,pitch] [--box x0,y0,x1,y1] [--interval MS]   per-pixel change between two captures apart, Vulkan and GL, sim running
  ab <cvar> <a> <b> [--spot NAME | --at x,y,z --face yaw,pitch] [--box x0,y0,x1,y1]   Vulkan under each value in ONE paused frame, plus a repeat as the noise floor
  costs [--port P] [--spot NAME | --at x,y,z --face yaw,pitch] [--frames N] [--rounds N] [--bench N]   what a frame costs at one view of a running instance: frame p50/p99, GPU and submit time, the world passes and phases, the walk's clipping, batches and sprites (the performance scope's step 0)
  abperf "<setting>" "<setting>" ... [--rounds N] [--frames N] [--spot NAME | --at x,y,z --face yaw,pitch]   frame time (p50/p99) under each console setting in turn inside one running session, round by round, each judged against the first within its round (default 3 rounds)
  shot <tag> [--port P] [--spot NAME | --at x,y,z --face yaw,pitch] [--force] [--same-frame]   matched GL/Vulkan pair from a running instance, one camera, sim frozen; --same-frame takes both from one engine frame, for an animated texture
  window [--shot gl|vk --out FILE] [--resize WxH [--port P]]   the engine's windows as Windows sees them (minimized, hung, where), with --shot what that window really shows, read from the compositor rather than GL's own buffer, and with --resize the instance's game window sized as dragging its corner does
  shoot [--port P] --tag T --at x,y,z --face yaw,pitch [--look x,y,z] [--weapon W] [--map M] [--after TICS]   fire at a junction and capture a GL/Vulkan pair (--look frames a point, --after catches transient effects before they fade)
  sweep [--maps "MAP01 MAP07"] [--port P]   matched pairs across several maps, ranked by how much the renderers disagree
  doorshot <tag> [--port P] [--at x,y,z --face yaw] [--mid TICS]   a door caught MID-SWING in both renderers, plus a before pair
  look [--port P] [--spot S | --at x,y,z --face yaw,pitch] [--pixel x,y [--size WxH]] [--stack]   what the crosshair is on and what the level mesh holds for it; --pixel aims through that pixel of a capture from the camera, --stack casts through the first stacked view
  line <index> | --at X,Y [--radius R]   what a linedef IS: blocking flags, front/back sector heights, and which tier carries a texture over which span of height -- the three things that decide whether a mark can exist there. --at finds every line near a point, which is what tells two lines in the same place apart.
  png <mode> ...                     pixel arithmetic on captures: --diff, --boxdiff, --diffimg, --align, --crop, --rows, --blob, --px, --range (a box's brightness and channel means, over several files)
  browser --port P [--token T] [--wait S] [--expect-lan] [--expect-country XXX]   refresh the server browser and report what it sees (LAN vs registry, country)
  hostdiag --port P [--token T] [--wait S] [--expect-listed]           ask the registry whether THIS server is reachable from outside (per family)
  continue --port P [--token T] [--expect shown|hidden]   what the Continue button is offering, and whether it is on the bar
  console <text...> [--port P]       run one console command on an instance and print what it printed
  stats [PREFIX] [--json | --watch] [--port P]   the engine's telemetry counters (fua_stats) whose names start with PREFIX: last, mean, p99 and max over the last frames; --watch puts them on screen
  record [--seconds N] [--name NAME] [--port P]   every counter each frame and every timed region on every thread for N seconds (fua_capture), as a CSV and a trace Perfetto opens, under the engine's telemetry/
  flight [NAME] [--port P]           write the flight recorder (the last frames, seconds and hitches) under the engine's telemetry/ and print its summary
  hitches [N] [--port P]             the last N slow frames, each with the timed regions it spent most in
  memsnap [NAME] [--port P]          keep every memory gauge and tag under NAME for memdiff
  memdiff [A] [B] [--port P]         what changed between snapshots A and B, or between A and now
  soak [--action reload|"STEP|STEP..."] [--cycles N] [--warmup W] [--settle MS] [--ignore PREFIX,...] [--judge-port P [--judge-token T]] [--sites] [--port P]   repeat an action's console steps N times, waiting on each that starts a level, and fail if memory, objects, handles or threads kept growing after W warm-up cycles (fua_soak) or the cycles got slower; a step "check: resize WxH" sizes the game window, --ignore leaves readings under those prefixes out, --judge-port judges a server this instance joins and leaves, and --sites prints the allocation stacks that grew
  compare A.json B.json [--share F] [--floor MS]   two fua_stats json readings side by side; a timing whose p50 or p99 grew by over F (0.15) and FLOOR ms (0.1) is marked
  perfgate NAME [PREFIX] [--spot S | --at x,y,z --face yaw,pitch] [--settle MS] [--update] [--port P]   the counters at a view against this machine's saved baseline NAME, the first run saving it; exit 1 on a regression
  mcp                                run as an MCP stdio server for agents
`;

async function main() {
  // [rc4l] A reader that stops early (`| head`) closes the pipe, which ends the command rather than crashing it.
  process.stdout.on("error", (e) => { if (e.code === "EPIPE") process.exit(0); throw e; });
  const [cmd, ...argv] = process.argv.slice(2);
  const { flags, rest } = parseFlags(argv);
  // [rc4l] --help says what a command takes and does nothing else: `play --help` used to launch an engine.
  if (flags.help) { console.log(usageFor(USAGE, cmd)); return; }

  switch (cmd) {
    case "ls": {
      const r = readRegistry();
      for (const e of r) console.log(`pid=${e.pid} port=${e.port ?? "?"} ppid=${e.ppid ?? "?"}`);
      if (!r.length) console.log("(no instances registered)");
      break;
    }
    case "reap": {
      const r = reap({ kill: !!flags.kill, all: !!flags.all });
      console.log(`orphans=${r.orphan.length} owned(left-alone)=${r.owned.length} killed=${r.killed.length} pruned=${r.prunedCount}`);
      break;
    }
    case "launch": {
      // [rc4l] --port/--token/--iwad/--skill are passed through rather than dropped. launchInstance
      // has always taken them; launch forwarded only map and seed, so `--port 7777` was accepted
      // silently and then ignored, and the caller had to read the chosen port back out of stdout.
      // A fixed port is the difference between scripting a run and parsing for it.
      const inst = await launchInstance({
        map: flags.map,
        seed: flags.seed != null ? Number(flags.seed) : undefined,
        port: flags.port != null ? Number(flags.port) : undefined,
        token: flags.token || undefined,
        iwad: flags.iwad || undefined,
        skill: flags.skill != null ? Number(flags.skill) : undefined,
        // [rc4l] --play hands the window back to the human at the keyboard.
        //
        // Bridge instances are hands-off by default: the input lock drops OS keyboard and mouse at
        // the message pump so a stray cursor over the window cannot diverge a deterministic run from
        // its twin. That is right for measuring and useless for the case where the point IS to walk
        // around and look at something, which until now needed the env var set by hand.
        allowOsInput: !!flags.play,
        // [rc4l] --res WxH, because a stutter report is resolution-shaped.
        //
        // launch was pinned to 1024x640 with no way to say otherwise, so a frame-time tail reported
        // on a full-screen window could only be reproduced at a third of the pixels -- where it is
        // milder, and where anything fill-bound hides entirely. `play` had --res; the measuring
        // command, which is the one that needs it, did not.
        ...(flags.res ? (() => {
          const m = String(flags.res).match(/^(\d+)x(\d+)$/i);
          if (!m) throw new Error(`--res wants WxH, e.g. 1280x800 (got ${flags.res})`);
          return { width: Number(m[1]), height: Number(m[2]) };
        })() : {}),
        // [rc4l] PWADs, comma-separated. Without this the only launchable thing was a bare IWAD, so
        // a mod could be profiled only by hosting a server first and measuring through the netcode.
        // [rc4l] --arg passes raw engine arguments through (repeat with commas), so a harness can
        // start a host, a dedicated server or any other mode without inventing a flag per mode.
        extraArgs: [
          ...(flags.file ? String(flags.file).split(",").flatMap((f) => ["-file", f.trim()]) : []),
          ...(flags.arg ? String(flags.arg).split(",").map((a) => a.trim()) : []),
        ],
        // [rc4l] --cvar name=value, repeatable as a comma-separated list. Applied before the map
        // loads, which is the only time some of them take effect (sv_nomonsters being the one that
        // matters for hands-off testing).
        cvars: shot.parseCvarFlag(flags.cvar),
      });
      console.log(`launched pid=${inst.pid} port=${inst.port} token=${inst.token}`);
      // Where to look when it dies. The bridge log is the console; the stdio log is how it died.
      console.log(`logs: ${inst.log}`);
      console.log(`      ${inst.stdioLog}`);
      console.log(`(rpc it with: fuactl rpc sim.tic --port ${inst.port} --token ${inst.token})`);
      process.on("SIGINT", async () => { await stopInstance(inst); process.exit(0); });
      await new Promise(() => {}); // stay up
      break;
    }
    // [rc4l] Advance the map rotation N times, confirming each intermission.
    //
    // Written because driving this by hand kept stalling. `nextmap` does not change level: it ends
    // the current one into an intermission that waits for a keypress, so a script that fires nextmap
    // and then polls for a new level waits forever. And a bug that only appears after several map
    // changes -- the material cache serving a dead level's textures -- cannot be reached any other
    // way: launching straight into the map that looks broken renders it perfectly.
    //
    // The advance is detected from leveltime going backwards, which is exact and needs nothing
    // parsed out of a log.
    //
    //   fuactl cycle --port P --token T [--count N] [--settle MS]
    case "cycle": {
      if (!flags.port) { console.error("usage: fuactl cycle --port P [--token T] [--count N]"); process.exit(2); }
      const count = num(flags.count) ?? 1;
      const settle = num(flags.settle) ?? 4000;
      const nap = (ms) => new Promise((r) => setTimeout(r, ms));
      await withUi(flags, async (c) => {
        for (let i = 0; i < count; i++) {
          // [rc4l] Unwrap whichever shape the rpc returns, and never trust one signal.
          const tic = async () => { const r = await c.rpc("sim.tic"); return r && r.result ? r.result : r; };
          const before = (await tic()).leveltime ?? 0;
          let sawIntermission = false;
          await c.rpc("console.exec", { text: "nextmap" });
          let ok = false;
          for (let t = 0; t < 60 && !ok; t++) {
            await nap(1000);
            const st = await tic();
            if (!st.inlevel) sawIntermission = true;
            // Either the clock went backwards (new level) or we left the level and came back.
            if (st.inlevel && ((st.leveltime ?? 0) < before || sawIntermission)) { ok = true; break; }
            // Still on the intermission, which waits to be dismissed. It watches the PLAYER's
            // buttons (BT_USE/BT_ATTACK), not the menu, so a menu key never reaches it -- that is
            // what stalled the first version of this at the very first map.
            await c.rpc("console.exec", { text: "+use" }).catch(() => {});
            await nap(120);
            await c.rpc("console.exec", { text: "-use" }).catch(() => {});
          }
          if (!ok) { console.error(`cycle: stalled after ${i} of ${count}`); process.exit(1); }
          await nap(settle);
          console.log(`cycle ${i + 1}/${count}`);
        }
        return { cycled: count };
      });
      break;
    }
    // [rc4l] `fuactl actors` -- what stands near a point or ahead of the player, out of the hundreds state.actors lists.
    case "actors": {
      const actorsUsage = "usage: fuactl actors [--near x,y] [--radius R] [--toward YAW | --ahead] [--cone DEG] [--class REGEX] [--port P --token T]";
      const c = await connectTarget(flags, actorsUsage);
      try {
        const cam = await c.rpc("player.camera");
        const [x, y] = flags.near ? String(flags.near).split(",").map(Number) : [cam.x, cam.y];
        const toward = flags.toward !== undefined ? Number(flags.toward) : (flags.ahead ? cam.yaw : null);
        const { actors } = await c.rpc("state.actors", { limit: 1000000 });
        const found = actorsNear(actors, { x, y, toward, radius: flags.radius ? Number(flags.radius) : Infinity,
          cone: flags.cone ? Number(flags.cone) : 20, cls: flags.class ? new RegExp(String(flags.class), "i") : null });
        for (const a of found)
          console.log(`${a.c.padEnd(28)} ${String(Math.round(a.d)).padStart(6)} away  bearing ${String(Math.round(a.bearing)).padStart(3)}  at ${a.x},${a.y},${a.z}`);
      } finally { c.close(); }
      break;
    }
    // [rc4l] `fuactl probe` -- a scenario's own map, at one of its vantages, with console commands run there: four scratch scripts' worth of setup.
    case "probe": {
      const probeUsage = "usage: fuactl probe <scenario.mjs> [--map M] [--vantage V] [--side S] [--engine path/to/forkundera.exe] [<command> ...]";
      if (!rest[0]) { console.error(probeUsage); process.exit(2); }
      if (flags.engine) process.env.FUACTL_ENGINE = String(flags.engine);
      const url = pathToFileURL(path.resolve(rest[0])).href;
      await import(url);
      const options = scenarioOptions(url);
      if (!options) { console.error(`${rest[0]} does not call runScenarioIfMain`); process.exit(2); }
      const out = await probeScenario(options(), { map: flags.map, vantage: flags.vantage, side: flags.side }, rest.slice(1));
      console.log(`engine ${resolveEngine()}`);
      for (const o of out) console.log(`> ${o.command}\n${o.output}`);
      break;
    }
    // [rc4l] `fuactl scenarios` -- the whole regression set as one command, where it was a shell loop over scenarios/*.mjs.
    case "scenarios": {
      if (flags.engine) process.env.FUACTL_ENGINE = String(flags.engine);
      const dir = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "scenarios");
      const files = templateScenarioFiles(dir, rest);
      // [rc4l] --side S --cvar k=v runs a scenario with one side changed, to show a regression test fails under the behaviour it guards against.
      const adjust = flags.webgpu ? webgpuOptions : flags.standalone ? standaloneOptions
        : flags.side ? sideAdjust(String(flags.side), shot.parseCvarFlag(flags.cvar) || {}) : undefined;
      console.log(`engine ${resolveEngine()}`);
      let failed = 0, skipped = 0;
      // [rc4l] --matrix k=a,b runs the suite once per value with the cvar on every side, the label naming which run a line came from.
      const runs = flags.matrix ? matrixSets(flags.matrix).map((set) => ({ label: Object.entries(set).map(([k, v]) => `${k}=${v}`).join(" "), adjust: matrixAdjust(set, adjust) }))
        : [{ label: "", adjust }];
      for (const r of runs) {
        if (r.label) console.log(`-- ${r.label}`);
        for (const f of files) {
          const url = pathToFileURL(path.join(dir, f)).href;
          await import(url);
          const run = scenarioRun(url, r.adjust);
          if (!run) continue;
          let out;
          // [rc4l] A scenario that throws is one failure, not the end of the run.
          try { out = await run(); } catch (e) { out = { lines: [`error: ${e.message}`], code: 1 }; }
          for (const l of out.lines.filter((x) => !x.startsWith("PASS"))) console.log(`${(r.label ? `${r.label} ` : "") + f.slice(0, -4).padEnd(18)} ${l}`);
          if (out.skipped) skipped++;
          if (out.code !== 0) failed++;
        }
      }
      const ran = skipped ? ` (${skipped} skipped)` : "";
      console.log(failed ? `${failed} scenario(s) failed${ran}` : `every scenario within its limits${ran}`);
      process.exitCode = failed ? 1 : 0;
      break;
    }
    // [rc4l] `fuactl burst` -- timed captures from one renderer with the sim running, for what moves: an animation's rate, a light a paused shot hides.
    case "burst": {
      const usage = "usage: fuactl burst <tag> [--side gl|vk] [--count N] [--box x0,y0,x1,y1] [--spot NAME | --at x,y,z --face yaw,pitch] [--port P]";
      const c = await connectTarget(flags, usage);
      const at = shot.cameraFromFlags(flags, SPOTS);
      if (at) await shot.moveTo(c, at);
      const out = await shot.captureBurst(c, {
        side: flags.side === "gl" ? "gl" : "vk", count: Number(flags.count) || 8,
        box: flags.box ? String(flags.box).split(",").map(Number) : [0, 0, 1, 1],
        engineBin: slashed(engineOf(flags)), tag: rest[0] || "burst",
      });
      for (const r of out) console.log(`${r.i} t=${r.t}ms mean=${r.mean.toFixed(2)} ${r.file || "(no capture)"}`);
      c.close();
      break;
    }
    // [rc4l] `fuactl window` -- the windows as Windows sees them and what one shows, instead of PowerShell beside the tool.
    case "window": {
      if (flags.shot && !["gl", "vk"].includes(String(flags.shot))) { console.error(usageFor(USAGE, "window")); process.exit(2); }
      if (flags.resize) {
        const [w, h] = String(flags.resize).split("x").map(Number);
        if (!(w > 0 && h > 0)) { console.error(usageFor(USAGE, "window")); process.exit(2); }
        const sized = resizeEngineWindow(sessionPid(flags), w, h);
        console.log(sized ? `sized the game window to ${w}x${h}` : "no game window for that instance");
        if (!sized) process.exit(1);
        break;
      }
      const r = engineWindows({ shot: flags.shot ? String(flags.shot) : null,
        out: flags.shot ? path.resolve(String(flags.out || `window_${flags.shot}.png`)) : null });
      for (const w of r.windows)
        console.log(`${(w.kind || "-").padEnd(3)} pid ${w.pid} ${w.minimized ? "minimized" : "shown"}${w.hung ? ", NOT RESPONDING" : ""} at ${w.rect.left},${w.rect.top}-${w.rect.right},${w.rect.bottom}  ${w.title}`);
      if (flags.shot) console.log(r.shot ? `${r.shot.ok ? "saved" : "FAILED (minimized or no picture)"} ${r.shot.width}x${r.shot.height} -> ${r.shot.out}` : `no ${flags.shot} window`);
      break;
    }
    // [rc4l] `fuactl steps <tag> --hold +crouch` -- a transition caught tic by tic, instead of a stepping script beside the tool.
    case "steps": {
      const usage = "usage: fuactl steps <tag> --hold +ACTION [--count N] [--spot NAME | --at x,y,z --face yaw,pitch] [--port P]";
      if (!flags.hold || !String(flags.hold).startsWith("+")) { console.error(usage); process.exit(2); }
      const c = await connectTarget(flags, usage);
      try {
        const at = shot.cameraFromFlags(flags, SPOTS);
        if (at) await shot.moveTo(c, at);
        const out = await shot.captureSteps(c, { tag: rest[0] || "steps", hold: String(flags.hold), count: Number(flags.count) || 8,
          engineBin: slashed(engineOf(flags)) });
        for (const s of out) console.log(`${s.name} viewz ${s.viewz} ${s.gl || "(no GL)"} ${s.vk || "(no Vulkan)"}`);
      } finally { c.close(); }
      break;
    }
    // [rc4l] `fuactl sector-of --pixel x,y` -- whose sector a pixel takes its colour from, a first-class command instead of a bisect script beside the tool.
    case "sector-of": {
      const usage = "usage: fuactl sector-of --pixel x,y [--side gl|vk] [--spot NAME | --at x,y,z --face yaw,pitch] [--port P]";
      if (!flags.pixel) { console.error(usage); process.exit(2); }
      const [x, y] = String(flags.pixel).split(",").map(Number);
      const c = await connectTarget(flags, usage);
      try {
        const at = shot.cameraFromFlags(flags, SPOTS);
        if (at) await shot.moveTo(c, at);
        const r = await sectorOf(c, { side: flags.side === "vk" ? "vk" : "gl", x, y,
          engineBin: slashed(engineOf(flags)), log: (m) => console.error(m) });
        console.log(r.sector === null ? `pixel ${x},${y} follows no sector's colour (sky, a fullbright, or a texel with no red or green)`
          : `sector ${r.sector} (of ${r.count}, ${r.frames} frames); the sectors stay painted until the map restarts`);
        process.exitCode = r.sector === null ? 1 : 0;
      } finally { c.close(); }
      break;
    }
    // [rc4l] `fuactl travel --to MAP` -- nextmap until the instance is on MAP, as a player reaches it, since some faults only follow a level change.
    case "travel": {
      const usage = "usage: fuactl travel --to MAP [--max N] [--port P]";
      if (!flags.to) { console.error(usage); process.exit(2); }
      const want = String(flags.to).toUpperCase();
      const c = await connectTarget(flags, usage);
      const mapOf = async () => String(((await c.rpc("player.camera").catch(() => ({}))) || {}).map || "").toUpperCase();
      let hops = 0;
      for (let on = await mapOf(); on !== want; on = await mapOf()) {
        if (hops++ >= (Number(flags.max) || 16)) { console.error(`still on ${on} after ${hops - 1} nextmap(s)`); process.exit(1); }
        await cap.exec(c, "nextmap");
        // [rc4l] The change starts on a later tic, so wait for the map to read differently before waiting for the level.
        for (const was = on, until = Date.now() + 30000; (await mapOf()) === was;) {
          if (Date.now() > until) { console.error(`nextmap left the instance on ${was}`); process.exit(1); }
          await new Promise((r) => setTimeout(r, 250));
        }
        await cap.waitInLevel(c, 60000);
      }
      console.log(`on ${want} after ${hops} nextmap(s)`);
      c.close();
      break;
    }
    // [rc4l] `fuactl queue add|list|next|note|done` -- problems pointed at in a live session, each saved as a spot, worked oldest first.
    case "queue": {
      const usage = "usage: fuactl queue add <description> [--port P] [--name SPOT] | list [--all] | next | note <id> <text> | done <id> [--unverified] [--note TEXT]";
      const [sub, ...words] = rest;
      const q = loadQueue();
      if (sub === "add" && words.length) {
        const description = words.join(" ");
        let where = { map: flags.map && flags.map !== true ? String(flags.map) : null, at: null };
        if (!flags["no-instance"]) {
          const c = await connectTarget(flags, usage);
          const cam = await captureSpot(c, flags.name ? String(flags.name) : spotNameFor(nextId(q), description), description);
          c.close();
          where = { map: cam.map, at: shot.atFromCamera(cam) };
        }
        const r = addItem(q, { description, spot: flags.name ? String(flags.name) : undefined, ...where });
        saveQueue(r.queue);
        console.log(formatItem(r.item));
      } else if (sub === "list") {
        for (const it of q.items.filter((x) => flags.all || x.status === "open")) console.log(formatItem(it));
      } else if (sub === "next") {
        const it = nextOpen(q);
        const db = JSON.parse(fs.readFileSync(SPOTS, "utf8"));
        console.log(it ? JSON.stringify({ ...it, spotRecord: db.spots[it.spot] || null }, null, 2) : "the queue is empty");
      } else if (sub === "note" && words.length > 1) {
        saveQueue(noteItem(q, words[0], words.slice(1).join(" ")));
      } else if (sub === "done" && words.length) {
        const closed = closeItem(q, words[0], { unverified: !!flags.unverified, note: flags.note ? String(flags.note) : null });
        saveQueue(closed);
        console.log(formatItem(closed.items.find((x) => x.id === Number(words[0]))));
      } else { console.error(usage); process.exit(2); }
      break;
    }
    // [rc4l] `fuactl quit` -- stop an instance by its token and wait for its port to close, where a bare console quit left no way to tell it had gone.
    case "quit": {
      const t = targetOf(flags);
      // [rc4l] --park first files where it stands, what it loaded and how it rendered, so `play --unpark` brings it back after an exclusive run.
      if (flags.park) {
        const c = await connectTarget(flags);
        const cam = await captureSpot(c, shot.PARKED, `parked from port ${t.port}`, shot.parkFileFor(SESSION(), t.port)).finally(() => c.close());
        console.log(`parked ${cam.map} at ${cam.x},${cam.y},${cam.z} facing ${cam.yaw},${cam.pitch}`);
      }
      let outcome = await quitInstance(t);
      // [rc4l] A launch whose session file is gone leaves no token, so its orphan on this port is stopped as `reap --kill` stops orphans.
      if (outcome === "refused" || outcome === "running") {
        const orphans = liveOnPort(readRegistry(), t.port, pidAlive).filter((e) => e.orphan);
        for (const e of orphans) { try { process.kill(e.pid, "SIGTERM"); } catch { /* gone already */ } }
        if (orphans.length) outcome = `stopped orphan pid ${orphans.map((e) => e.pid).join(", ")}`;
      }
      console.log(`${outcome}: port ${t.port}`);
      process.exitCode = outcome === "running" || outcome === "refused" ? 1 : 0;
      break;
    }
    case "rpc": {
      const rpcCmd = rest[0];
      const rpcUsage = "usage: fuactl rpc <cmd> [jsonArgs] [--port P --token T]";
      if (!rpcCmd) { console.error(rpcUsage); process.exit(2); }
      const args = rest[1] ? JSON.parse(rest[1]) : undefined;
      const c = await connectTarget(flags, rpcUsage);
      const res = await c.rpc(rpcCmd, args);
      console.log(JSON.stringify(res, null, 2));
      c.close();
      break;
    }
    case "session": {
      const report = await runDeterminismCheck({
        instances: flags.instances ? Number(flags.instances) : 2,
        seed: flags.seed ? Number(flags.seed) : undefined,
        map: flags.map || undefined,
        tics: flags.tics ? Number(flags.tics) : undefined,
        log: (m) => console.error(`[session] ${m}`),
      });
      console.log(JSON.stringify(report, null, 2));
      process.exit(report.pass ? 0 : 1);
      break;
    }
    case "perf-ab": {
      const report = await runPerfAblation({
        seed: flags.seed ? Number(flags.seed) : undefined,
        map: flags.map || undefined,
        iwad: flags.iwad || undefined,
        spawn: flags.spawn || undefined,
        count: flags.count ? Number(flags.count) : undefined,
        frames: flags.frames ? Number(flags.frames) : undefined,
        log: (m) => console.error(`[perf-ab] ${m}`),
      });
      console.log(JSON.stringify(report, null, 2));
      break;
    }
    case "sample": {
      // function-level hotspots of a running instance (by --pid, or --port -> registry lookup)
      let pid = flags.pid ? Number(flags.pid) : null;
      if (!pid && flags.port) {
        const live = liveOnPort(readRegistry(), flags.port, pidAlive);
        if (live.length) pid = live[0].pid;
      }
      if (!pid) { console.error("usage: fuactl sample --pid P | --port P [--seconds N]"); process.exit(2); }
      const opts = {
        seconds: flags.seconds ? Number(flags.seconds) : 2,
        top: flags.top ? Number(flags.top) : 12,
        engineOnly: !!flags.engine,
        stacks: !!flags.stacks,
      };
      // [rc4l] The Windows backend lives inside the engine, so it needs a connection rather than a
      // pid. Opened only when --port was given; the external backends never touch it.
      const r = flags.port
        ? await withUi(flags, (c) => sampleProcess(pid, { ...opts, conn: c }))
        : await sampleProcess(pid, opts);
      console.log(JSON.stringify(r, null, 2));
      break;
    }
    case "hang": {
      // [rc4l] An instance that stops answering looks the same whether it died, wedged inside one
      // function, froze its tic, or is just paused. Probe, classify, and -- when it is wedged --
      // sample it so the report names the function to go read.
      if (!flags.port && !flags.pid) { console.error("usage: fuactl hang --port P [--token T] [--pid P]"); process.exit(2); }
      const r = await diagnoseHang({
        // num() so a bare `--pid` (no value) stays unset instead of becoming pid 1.
        port: num(flags.port) ?? null,
        token: flags.token || null,
        pid: num(flags.pid) ?? null,
        gapMs: num(flags.gap) ?? 1500,
        // [rc4l] Windows has no sampler outside the engine, so the stack dump helper stands in for a wedged one.
        _stacks: process.platform === "win32"
          ? (pid) => nativeStacks(pid, { exe: ensureNative("stackdump", shot.pickCmake(), { log: (m) => console.error(`[hang] ${m}`) }) })
          : null,
      });
      console.log(JSON.stringify(r, null, 2));
      if (!r.healthy) process.exitCode = 1;
      break;
    }
    case "doctor": {
      // [rc4l] fuactl and the MCP resolve the engine differently (FUACTL_ENGINE vs ZANDRONUM_EXE),
      // so check both: a release build sitting in one of those paths cannot be driven at all, and
      // says so with "bridge port never opened" rather than anything about the bridge.
      const src = flags.src || path.resolve(process.cwd(), "src/zandronum/src");
      const srcDir = fs.existsSync(src) ? src : null;
      const targets = [];
      if (flags.exe) targets.push({ label: "--exe", exe: String(flags.exe) });
      else {
        let fuactlExe = null;
        try { fuactlExe = resolveEngine(); } catch { /* reported as a missing engine below */ }
        targets.push({ label: "fuactl (FUACTL_ENGINE)", exe: fuactlExe });
        if (process.env.ZANDRONUM_EXE && process.env.ZANDRONUM_EXE !== fuactlExe) {
          targets.push({ label: "MCP (ZANDRONUM_EXE)", exe: process.env.ZANDRONUM_EXE });
        }
      }
      const reports = targets.map((t) => doctorEngine({ ...t, srcDir }));
      console.log(JSON.stringify(reports, null, 2));
      if (reports.some((r) => r.problems.length)) process.exitCode = 1;
      break;
    }
    case "gl-timers": {
      // GPU render profiling of a running instance: which GL pass is eating the frame?
      if (!flags.port) { console.error("usage: fuactl gl-timers --port P [--token T] [--frames N] [--warmup M]"); process.exit(2); }
      const { info, report } = await runGlTimers({
        port: Number(flags.port),
        token: flags.token || undefined,
        frames: flags.frames ? Number(flags.frames) : undefined,
        warmup: flags.warmup != null ? Number(flags.warmup) : undefined,
      });
      console.error(`[gl-timers] ${summarizeGlTimers(report)}`);
      console.log(JSON.stringify({ info, report }, null, 2));
      break;
    }
    // [rc4l] capture and ticprof reply immediately and deliver their numbers as an EVENT, so `rpc`
    // closes the socket before the report exists and can never see one. These hold the connection.
    case "capture": {
      if (!flags.port) { console.error("usage: fuactl capture --port P [--token T] [--frames N] [--warmup M]"); process.exit(2); }
      const report = await withUi(flags, (c) => cap.capturePerf(c, { frames: num(flags.frames) ?? 300, warmup: num(flags.warmup) ?? 0 }));
      console.error(`[capture] p50=${report.total.p50_ms}ms p99=${report.total.p99_ms}ms max=${report.total.max_ms}ms (sim ${report.sim_mean_ms.toFixed(2)} / render ${report.render_mean_ms.toFixed(2)})`);
      const note = refreshNote(report.total.p50_ms);
      if (note) console.error(`[capture] p50 ${note}`);
      console.log(JSON.stringify(report, null, 2));
      break;
    }
    case "fps": {
      if (!flags.map) { console.error(usageFor(USAGE, "fps")); process.exit(2); }
      const rows = await runFps({
        modes: fpsModes(flags.modes),
        rounds: num(flags.rounds) ?? 3, frames: num(flags.frames) ?? 600, warmup: num(flags.warmup) ?? 300,
        vsync: !!flags.vsync, sample: num(flags.sample), shot: !flags["no-shot"], label: flags.label || "run",
        launch: {
          port: num(flags.port), map: flags.map, iwad: flags.iwad, file: flags.file,
          ...resFlag(flags.res),
          engine: flags.engine && flags.engine !== true ? String(flags.engine) : undefined,
          extra: shot.parseCvarFlag(flags.cvar) || {},
        },
      }, { log: (line) => console.error(`[fps] ${line}`) });
      console.log(JSON.stringify(rows, null, 2));
      break;
    }
    case "screen": {
      if (!flags.port) { console.error(usageFor(USAGE, "screen")); process.exit(2); }
      // [rc4l] The port's own session file names its engine process, token given or not.
      const session = shot.readSession(shot.sessionFileFor(SESSION(), Number(flags.port)));
      const pid = num(flags.pid) ?? (session && session.pid);
      if (!pid) { console.error("screen needs the engine's process: launch it with play (which records it) or pass --pid"); process.exit(2); }
      const exe = ensureNative("screencap", shot.pickCmake(), { log: (m) => console.error(`[screen] ${m}`) });
      const out = path.join(os.tmpdir(), `fuactl-screen-${pid}.csv`);
      const traceOut = path.join(os.tmpdir(), `fuactl-screen-trace-${pid}.csv`);
      const result = await withUi(flags, (c) => screen.captureScreen(c, {
        pid, exe, out, traceOut, exec: cap.exec,
        seconds: num(flags.seconds) ?? 8, orbit: flags.orbit !== undefined ? Number(flags.orbit) : 6, period: num(flags.period) ?? 1.5,
        windowClass: flags.class && flags.class !== true ? String(flags.class) : null,
      }));
      console.error(`[screen] ${screen.formatCapture(result)}`);
      if (result.trace) console.error(`[screen] ${screen.formatTrace(result.trace)}`);
      console.log(JSON.stringify(result, null, 2));
      break;
    }
    case "trace": {
      if (!flags.port) { console.error(usageFor(USAGE, "trace")); process.exit(2); }
      const out = flags.out && flags.out !== true ? String(flags.out) : path.join(os.tmpdir(), `fuactl-trace-${flags.port}.csv`);
      const rows = await withUi(flags, (c) => trace.recordViewTrace(c, { seconds: num(flags.seconds) ?? 10, out, exec: cap.exec }));
      const analysis = trace.analyzeViewTrace(rows);
      for (const line of trace.formatViewTrace(analysis)) console.error(`[trace] ${line}`);
      console.error(`[trace] every frame: ${out}`);
      console.log(JSON.stringify(analysis, null, 2));
      break;
    }
    case "present": {
      const exe = present.ensurePresentMon({ log: (m) => console.error(`[present] ${m}`) });
      const seconds = num(flags.seconds) ?? 15;
      const out = flags.out && flags.out !== true ? path.resolve(String(flags.out)) : path.join(os.tmpdir(), `fuactl-present-${Date.now()}.csv`);
      const ps1 = path.join(os.tmpdir(), "fuactl-present.ps1");
      fs.rmSync(out, { force: true });
      fs.writeFileSync(ps1, present.adminScript(exe, { out, seconds, processName: flags.process && flags.process !== true ? String(flags.process) : null }));
      console.error(`[present] approve the Windows admin prompt; PresentMon then records ${seconds} s`);
      let traceRows = null, app = null;
      if (flags.port) {
        // [rc4l] The instance's own frames for exactly as long as PresentMon runs, on the same clock.
        const session = shot.readSession(shot.sessionFileFor(SESSION(), Number(flags.port)));
        app = path.basename((session && session.engine) || "forkundera.exe");
        traceRows = await withUi(flags, (c) => trace.recordViewTrace(c, { out: path.join(os.tmpdir(), `fuactl-present-trace-${flags.port}.csv`), exec: cap.exec,
          during: () => present.runAdminScript(ps1) }));
      } else await present.runAdminScript(ps1);
      if (!fs.existsSync(out)) { console.error("[present] PresentMon wrote nothing: was the admin prompt declined, or was nothing drawing?"); process.exit(1); }
      const rows = present.parsePresentMon(fs.readFileSync(out, "utf8"));
      const qpcHz = present.qpcFrequency();
      const analysis = present.analyzePresentMon(rows, { qpcHz });
      for (const line of present.formatPresentMon(analysis)) console.error(`[present] ${line}`);
      const bySecond = traceRows ? present.alignSeconds(rows, traceRows, { app, qpcHz }) : null;
      if (bySecond) {
        console.error(`[present] second by second, ${app}: drawn by the engine / handed to Windows / shown`);
        for (const s of bySecond) console.error(`[present]   ${String(s.second).padStart(3)}  ${String(s.drawn).padStart(5)} ${String(s.handed).padStart(5)} ${String(s.shown).padStart(5)}`);
      }
      console.error(`[present] every frame: ${out}`);
      console.log(JSON.stringify({ ...analysis, bySecond: bySecond || undefined }, null, 2));
      break;
    }
    case "ticprof": {
      if (!flags.port) { console.error("usage: fuactl ticprof --port P [--token T] [--tics N]"); process.exit(2); }
      const report = await withUi(flags, async (c) => {
        const done = c.waitEvent("ticprof", 120000);
        await c.rpc("perf.ticprof", { tics: num(flags.tics) ?? 80 });
        return done;
      });
      const tics = report.tics || [];
      const worst = [...tics].sort((a, b) => (b.total || 0) - (a.total || 0))[0];
      if (worst) console.error(`[ticprof] worst tic ${worst.total}ms (P_Ticker ${worst.pticker} / thinkers ${worst.thinkers} / effects ${worst.effects})`);
      console.log(JSON.stringify(report, null, 2));
      break;
    }
    case "simtrace": {
      if (!flags.port || !flags.out) { console.error("usage: fuactl simtrace --port P [--token T] --out FILE [--tics N]"); process.exit(2); }
      const tics = num(flags.tics) ?? 350;
      const out = path.resolve(String(flags.out));
      const report = await withUi(flags, async (c) => {
        const done = c.waitEvent("trace", 60000 + tics * 200);
        await c.rpc("sim.trace", { tics, path: out });
        return done;
      });
      console.log(JSON.stringify(report));
      break;
    }
    case "tracediff": {
      if (rest.length < 2) { console.error(usageFor(USAGE, "tracediff")); process.exit(2); }
      const [a, b] = rest.slice(0, 2).map((f) => parseSimTrace(fs.readFileSync(String(f), "utf8")));
      const d = diffSimTraces(a, b);
      for (const line of describeSimTraceDiff(d)) console.log(line);
      if (!d.same) process.exit(1);
      break;
    }
    case "fixture": {
      const [what, ...names] = rest;
      if (!["record", "trace", "check", "pictures", "vischeck"].includes(what)) { console.error(usageFor(USAGE, "fixture")); process.exit(2); }
      const list = fixtures.pickFixtures(names);
      const opts = { engine: engineOf(flags), store: presetDirs(flags).storeDir, cvars: shot.parseCvarFlag(flags.cvar) || {}, update: !!flags.update,
        log: (m) => console.error(`[fixture] ${m}`) };
      if (what === "record") {
        for (const f of list) console.log(`${f.name}: ${await fixtures.recordFixture(f, opts)}`);
      } else if (what === "trace") {
        for (const f of list) {
          const out = flags.out && list.length === 1 ? path.resolve(String(flags.out)) : path.join(fixtures.fixtureDir(), `${f.name}.latest.trace`);
          console.log(`${f.name}: ${await fixtures.traceFixture(f, out, opts)}`);
        }
      } else if (what === "check") {
        const results = await fixtures.checkFixtures(list, opts);
        for (const r of results) for (const line of r.lines) console.log(`${r.name}: ${line}`);
        if (results.some((r) => r.same === false)) process.exit(1);
      } else {
        // [rc4l] Pictures and the walk check run one fixture at a time, each its own launch.
        const run = what === "pictures" ? (f) => fixtures.pictureFixture(f, { ...opts, tolerance: num(flags.tolerance) ?? 2 })
          : async (f) => [await fixtures.visCheckFixture(f, opts)];
        let failed = false;
        for (const f of list) for (const r of await run(f)) { console.log(`${r.name}: ${r.line}`); if (r.same === false) failed = true; }
        if (failed) process.exit(1);
      }
      break;
    }
    case "bench": {
      if (!flags.port) { console.error("usage: fuactl bench --port P --scenario FILE.json [--runs N] [--metric total.p99_ms]"); process.exit(2); }
      if (!flags.scenario) { console.error("bench needs --scenario FILE.json (see scenarios/)"); process.exit(2); }
      const scenario = JSON.parse(fs.readFileSync(String(flags.scenario), "utf8"));
      const result = await withUi(flags, (c) => runBench(c, scenario, {
        runs: num(flags.runs) ?? 5,
        metric: flags.metric ? String(flags.metric) : "total.p99_ms",
        log: (m) => console.error(`[bench] ${m}`),
      }));
      if (result.summary) {
        const s = result.summary;
        console.error(`[bench] ${result.metric}: median ${s.median}, range ${s.min}-${s.max}, sd ${s.stddev} over ${s.n} valid runs`);
      }
      // A bench that kept nothing is a failure, not an empty result -- exit non-zero so a script notices.
      console.log(JSON.stringify(result, null, 2));
      if (!result.runs_kept) process.exit(1);
      break;
    }
    case "diligent": {
      if (!flags.port) { console.error("usage: fuactl diligent --port P [--token T] [--frames N] [--shot FILE] [--sweep DIR] [--scale]"); process.exit(2); }
      // [rc4l] --sweep writes one shot per fua_dg_lightmode debug view into DIR, all from the same
      // upload and the same camera, so the shaded view and the depth view are actually comparable.
      const sweep = flags.sweep && flags.sweep !== true
        ? [0, 1, 2, 3, 4, 5, 6, 7, 8, 9].map((mode) => ({ mode, file: `${flags.sweep}/lm${mode}.png` }))
        : [];
      await diligentRun({
        port: flags.port,
        token: flags.token || null,
        frames: flags.frames ? Number(flags.frames) : undefined,
        shot: flags.shot || null,
        sweep,
        scale: !!flags.scale,
        bakeAll: !!flags["bake-all"],
        log: (m) => console.error(`[diligent] ${m}`),
      });
      console.error("[diligent] done -- results are in the engine log (fua_diligent_bench lines)");
      break;
    }
    // [rc4l] Query the level's linedefs and print the matches. The engine prints to its console, so
    // this runs the CCMD and reads the lines back out of the instance's log -- the same shape
    // `fuactl doorshot` uses. Driving the engine to a specific piece of geometry (a door, a switch, a
    // trigger) used to mean walking the level blind; this makes it two commands.
    case "lines": {
      if (!flags.port) {
        console.error("usage: fuactl lines --port P [--token T] [--special N] [--door] [--use]" +
                      " [--cross] [--tag N] [--texture NAME] [--limit N]");
        process.exit(2);
      }
      const parts = [];
      if (flags.special != null && flags.special !== true) parts.push(`special=${flags.special}`);
      if (flags.tag != null && flags.tag !== true) parts.push(`tag=${flags.tag}`);
      if (flags.door) parts.push("door=1");
      if (flags.use) parts.push("use=1");
      if (flags.cross) parts.push("cross=1");
      if (flags.texture && flags.texture !== true) parts.push(`texture=${flags.texture}`);
      parts.push(`limit=${flags.limit && flags.limit !== true ? flags.limit : 8}`);

      // [rc4l] The bridge streams the console's output back, so the lines arrive with the answer rather than out of the engine's log.
      const text = await withUi(flags, (c) => cap.exec(c, `fua_find_lines ${parts.join(" ")}`, { quietMs: 300, maxMs: 8000 }));
      for (const l of text.split(/\r?\n/)) {
        if (/^line \d+:|^fua_find_lines:|^no level loaded/.test(l)) console.log(l);
      }
      break;
    }
    // [rc4l] `fuactl here` -- the live camera, in the units a capture takes.
    //
    // A bug report is a camera: a place someone was standing and a direction they were looking. Every
    // other route to one loses the precision that decides whether the repro lands on the step being
    // described or the step above it. Coordinates read off a screenshot are integers; the direction
    // vector fua_look prints is rounded to two decimals, which at three hundred units is fifteen
    // units of miss. The same reported fault got reproduced in three slightly wrong places before
    // this existed.
    //
    // Read-only, so it is safe against an instance somebody is playing -- which is the point. The
    // person who found the fault should not have to stop and read numbers off their own screen.
    //
    //   fuactl here --port P                 print it, with a ready-made capture line
    //   fuactl here --port P --save NAME     ...and record it in spots.json as a named repro
    case "here": {
      const c = await connectTarget(flags, "usage: fuactl here [--port P --token T] [--save NAME] [--note TEXT]");
      const cam = flags.save ? await captureSpot(c, String(flags.save), flags.note) : await c.rpc("player.camera");
      c.close();
      console.log(JSON.stringify(cam, null, 2));
      console.error(`\n  fuactl shot <tag> --at ${cam.x},${cam.y},${cam.z} --face ${cam.yaw},${cam.pitch}`);
      break;
    }
    // [rc4l] `fuactl png <mode> ...` -- the pixel arithmetic, on the same surface as everything else.
    case "png": {
      // [rc4l] RAW argv, not the parsed flags. png's modes are spelled `--crop`, `--diff`, `--rows`,
      // which parseFlags reads as flags -- so it swallowed the mode AND the filename after it, and
      // png saw a list starting with a fraction. Its own parser wants the words as typed.
      png(process.argv.slice(3));
      break;
    }
    // [rc4l] `fuactl look` -- what the crosshair is on, and what the mesh holds for it.
    // [rc4l] `fuactl line` -- what the engine thinks a linedef is.
    //
    // A wall that takes bullets and refuses rockets, a decal with nowhere to go, a projectile
    // stopping in mid-air: all three are answered by a line's flags and its tier spans, and none
    // of them could be asked from a running game.
    case "line": {
      const c = await connectTarget(flags);
      try {
        let cmd;
        if (flags.at) {
          const [x, y] = String(flags.at).split(",").map(Number);
          cmd = `fua_lines_at ${x} ${y} ${flags.radius || 32}`;
        } else {
          const idx = process.argv[3];
          if (idx === undefined || idx.startsWith("--")) {
            console.error("usage: fuactl line <index> | fuactl line --at X,Y [--radius R]");
            process.exit(2);
          }
          cmd = `fua_line ${Number(idx)}`;
        }
        process.stdout.write(await cap.exec(c, cmd, { quietMs: 500, maxMs: 20000 }));
      } finally { c.close(); }
      break;
    }
    case "look": {
      const c = await connectTarget(flags);
      try {
        let cam = shot.cameraFromFlags(flags, SPOTS);
        // [rc4l] --pixel with no camera aims from where the player stands, rather than ignoring the pixel and reading the crosshair.
        if (flags.pixel && !cam) cam = shot.atFromCamera(await c.rpc("player.camera", {}));
        // [rc4l] --pixel turns a point on a capture into the facing that puts the crosshair on it, instead of a hand-worked angle.
        if (flags.pixel && cam) {
          const [px, py] = String(flags.pixel).split(",").map(Number);
          const { width: w, height: h } = resFlag(flags.size || "1024x640");
          cam = { ...cam, ...pixelToFace(cam, px, py, w, h) };
        }
        process.stdout.write(await sweepMod.look(c, cam, flags.stack ? "fua_look stack" : "fua_look"));
      } finally { c.close(); }
      break;
    }
    // [rc4l] `fuactl sweep` -- matched pairs across several maps, so the next thing to fix is the
    // worst number rather than the most interesting-sounding entry on a feature list.
    case "sweep": {
      const maps = flags.maps ? String(flags.maps).split(/[,\s]+/).filter(Boolean) : undefined;
      const pairs = await sweepMod.sweep({ maps, port: flags.port ? Number(flags.port) : undefined });
      if (pairs.length) png(["--diff", ...pairs.flatMap((p) => [p.gl, p.vk])]);
      break;
    }
    // [rc4l] `fuactl doorshot` -- a door caught mid-swing, which is the only state that shows
    // whether moving geometry is tracked. A still level cannot answer it.
    case "doorshot": {
      const c = await connectTarget(flags);
      try {
        const out = await sweepMod.doorShot(c, rest[0] || "door", {
          at: shot.cameraFromFlags(flags, SPOTS, "90,0") || undefined, engineBin: engineOf(flags),
          midTics: flags.mid ? Number(flags.mid) : undefined,
        });
        console.log(JSON.stringify(out, null, 2));
      } finally { c.close(); }
      break;
    }
    // [rc4l] `fuactl build` -- compile and stage, failing loudly rather than staging nothing.
    case "build": {
      const r = shot.build({ root: flags.root || undefined, stage: !flags["no-stage"], tests: !!flags.tests });
      if (r.tests) console.log(`unit tests: ${r.tests.passed} passed`);
      if (!r.staged) { console.log("build ok, not staged"); break; }
      const when = new Date(fs.statSync(r.staged).mtime).toTimeString().slice(0, 8);
      console.log(`build ok, staged ${when} (+${r.entries} catalogue entries)`);
      break;
    }
    case "crashes": {
      const hours = flags.hours && flags.hours !== true ? Number(flags.hours) : 24;
      const found = recentCrashes(Date.now() - hours * 3600 * 1000);
      for (const c of found) {
        const { why, seams, stack } = summarizeCrash(fs.readFileSync(c.file, "utf8"));
        console.log(`${new Date(c.at).toISOString()} ${c.file}
  ${why}`);
        if (seams) console.log(`  seams: ${seams}`);
        for (const f of stack) console.log(`    ${f}`);
      }
      console.log(`${found.length} crash file(s) in the last ${hours} hour(s)`);
      break;
    }
    case "comments": {
      const issues = commentIssues(changeSince(fileURLToPath(new URL("../../../", import.meta.url)), flags.since && flags.since !== true ? String(flags.since) : "HEAD"));
      for (const i of issues) console.log(`${i.file}:${i.line}: ${i.why}: ${i.text.trim().slice(0, 140)}`);
      console.log(`${issues.length} comment(s) past one line or one sentence`);
      process.exit(issues.length ? 1 : 0);
    }
    // [rc4l] `fuactl privacy` -- what a change adds that could identify its author, run before every commit.
    case "privacy": {
      const since = flags.since && flags.since !== true ? String(flags.since) : "HEAD";
      const issues = privacyIssues(changeSince(fileURLToPath(new URL("../../../", import.meta.url)), since, { staged: !!flags.staged }), machinePatterns());
      for (const i of issues) console.log(`${i.file}:${i.line}: ${i.match}`);
      console.log(`${issues.length} line(s) that could identify you (your own patterns: ${patternsPath()})`);
      process.exit(issues.length ? 1 : 0);
    }
    case "coverage": {
      const { results, report } = runCoverage({ root: fileURLToPath(new URL("../../../", import.meta.url)), tool: flags.tool, since: flags.since && flags.since !== true ? String(flags.since) : null });
      const bad = results.filter((r) => !r.ok);
      for (const r of bad) console.log(`${r.unit}: ${r.unseen ? "not in the report" : `${r.hit}/${r.total}, missed lines ${r.missed.slice(0, 25).join(",")}`}`);
      console.log(`${results.length} unit(s) gated, ${bad.length} below 100% (report ${report})`);
      process.exit(bad.length ? 1 : 0);
    }
    // [rc4l] `fuactl mapdata <MAP>` -- a level's sectors, lines, specials and ACS source read from its files, no engine needed.
    case "mapdata": {
      const map = rest[0];
      const mdUsage = "usage: fuactl mapdata <MAP> (--preset ID [--variant V --store DIR] | --file a.pk3,b.wad) [--sector N,..] [--line N,..] [--special N] [--tag N] [--thing TYPE,..] [--scripts REGEX]";
      if (!map || (!flags.preset && !flags.file)) { console.error(mdUsage); process.exit(2); }
      const { catalogueDir, storeDir } = presetDirs(flags);
      const paths = flags.preset ? resolvePreset(catalogueDir, storeDir, flags.preset, flags.variant)
        : String(flags.file).split(",").map((f) => f.trim()).filter(Boolean);
      const found = findMapLumps(paths.map((p) => ({ name: path.basename(p), buf: fs.readFileSync(p) })), map);
      if (!found) { console.error(`${map} is in none of: ${paths.map((p) => path.basename(p)).join(", ")}`); process.exit(1); }
      const list = (v) => (v ? String(v).split(",").map(Number) : []);
      console.log(JSON.stringify(mapReport(found, { sectors: list(flags.sector), lines: list(flags.line),
        scripts: flags.scripts ? String(flags.scripts) : null, special: flags.special ? Number(flags.special) : null,
        tag: flags.tag !== undefined ? Number(flags.tag) : null, things: list(flags.thing) }), null, 2));
      break;
    }
    // [rc4l] `fuactl ready` -- wait until an instance answers, instead of a hand-rolled poll after every launch.
    case "ready": {
      const deadline = Date.now() + 1000 * (Number(flags.timeout) || 120);
      // [rc4l] The bridge serves only its newest client, so this tries only a live session's token (liveTarget), read again each round: connecting early knocked a launch still checking its world off the bridge, and it died.
      let failure = null;
      for (;;) {
        const t = sessionTarget(flags);
        if (shot.liveTarget(t)) {
          const c = new BridgeClient();
          try {
            await c.connect(t.port, { token: t.token, timeoutMs: 1500 });
            await c.waitHello(3000);
            const cam = await c.rpc("player.camera");
            console.log(JSON.stringify({ port: t.port, map: cam.map }));
            break;
          } catch (e) { failure = `port ${t.port} never answered: ${e.message}`; } finally { c.close(); }
        }
        if (Date.now() > deadline) {
          console.error(failure || "usage: fuactl ready [--port P --token T] [--timeout S] -- no live play session appeared");
          process.exit(failure ? 1 : 2);
        }
        await new Promise((r) => setTimeout(r, 500));
      }
      break;
    }
    // [rc4l] The views that are one console command each (telemetry.mjs), their output printed as the engine wrote it.
    case "console": case "stats": case "hitches": case "flight": case "memsnap": case "memdiff": {
      const text = telemetry.consoleTextFor(cmd, rest, flags);
      process.stdout.write(await withUi(flags, (c) => telemetry.execSlow(c, text)));
      break;
    }
    case "record": {
      const seconds = num(flags.seconds) ?? 10;
      const name = flags.name && flags.name !== true ? String(flags.name) : "";
      await withUi(flags, async (c) => {
        process.stdout.write(await cap.exec(c, `fua_capture start ${name}`.trim()));
        await cap.sleep(seconds * 1000);
        process.stdout.write(await cap.exec(c, "fua_capture stop", { quietMs: 600, maxMs: 60000 }));
      });
      break;
    }
    case "soak": {
      // [rc4l] --judge-port soaks a server: the action runs on this instance, a client joining and leaving it, and the server's memory is judged.
      const judge = flags["judge-port"] ? await connectTarget({ port: flags["judge-port"], token: flags["judge-token"] }, usageFor(USAGE, "soak")) : null;
      const result = await withUi(flags, (c) => telemetry.runSoak(c, {
        judge: judge ?? c,
        resize: async (w, h) => { if (!resizeEngineWindow(sessionPid(flags), w, h)) throw new Error(`soak: no game window to size to ${w}x${h}`); },
        action: flags.action && flags.action !== true ? String(flags.action) : "reload",
        cycles: num(flags.cycles) ?? 6, warmup: num(flags.warmup) ?? 1, settleMs: num(flags.settle) ?? 4000,
        ignore: flags.ignore && flags.ignore !== true ? String(flags.ignore).split(",") : [],
        sites: !!flags.sites,
        log: (m) => console.error(`[soak] ${m}`),
      }));
      if (judge) judge.close();
      process.stdout.write(result.report);
      for (const line of result.slower) console.log(`  slower: ${line}`);
      for (const s of result.sites) console.log(`  +${s.grew} (now ${s.count})  ${s.stack}`);
      if (!result.pass || result.slower.length) process.exit(1);
      break;
    }
    case "compare": {
      if (rest.length < 2) { console.error(usageFor(USAGE, "compare")); process.exit(2); }
      const read = (f) => telemetry.parseConsoleJson(fs.readFileSync(f, "utf8"));
      const rows = telemetry.compareStats(read(rest[0]), read(rest[1]), { share: num(flags.share), floor: num(flags.floor) });
      for (const line of telemetry.formatCompare(rows)) console.log(line);
      break;
    }
    case "perfgate": {
      if (!rest[0]) { console.error(usageFor(USAGE, "perfgate")); process.exit(2); }
      const at = shot.cameraFromFlags(flags, SPOTS);
      const readings = await withUi(flags, async (c) => {
        if (at) await shot.moveTo(c, at);
        await cap.sleep(num(flags.settle) ?? 4000);
        return telemetry.parseConsoleJson(await telemetry.execSlow(c, `fua_stats json ${rest[1] ?? ""}`.trim()));
      });
      const gate = telemetry.perfGate(rest[0], readings, { update: !!flags.update, share: num(flags.share), floor: num(flags.floor) });
      if (gate.saved) { console.log(`perfgate: saved ${readings.length} counters as the baseline ${gate.saved}`); break; }
      for (const line of telemetry.formatCompare(gate.rows)) console.log(line);
      console.log(`perfgate ${rest[0]}: ${gate.pass ? "PASS" : "FAIL"}`);
      if (!gate.pass) process.exit(1);
      break;
    }
    // [rc4l] `fuactl goto` -- stand at a saved spot or a camera, for looking rather than capturing.
    case "goto": {
      const at = shot.cameraFromFlags(flags, SPOTS);
      if (!at) { console.error("usage: fuactl goto (--spot NAME | --at x,y,z --face yaw,pitch) [--port P --token T]"); process.exit(2); }
      const c = await connectTarget(flags);
      try {
        await shot.moveTo(c, at);
        console.log(JSON.stringify(await c.rpc("player.camera"), null, 2));
      } finally { c.close(); }
      break;
    }
    // [rc4l] `fuactl ab <cvar> <a> <b>` -- one Vulkan frame under each value, captured in a single pause, with the repeat as its noise floor.
    // [rc4l] `fuactl motion` -- does a region move over time, in Vulkan and in GL, judged per pixel: a warp, a scroller, an animation.
    case "motion": {
      const motionUsage = "usage: fuactl motion [--spot NAME | --at x,y,z --face yaw,pitch] [--box x0,y0,x1,y1] [--interval MS]";
      const c = await connectTarget(flags, motionUsage);
      try {
        const engineBin = engineOf(flags);
        const got = await shot.captureOverTime(c, { at: shot.cameraFromFlags(flags, SPOTS), engineBin, gl: true,
          intervalMs: Number(flags.interval) || 600, tag: flags.tag || "motion" });
        const box = flags.box ? String(flags.box).split(",").map(Number) : [0, 0.05, 1, 1];
        const moved = (pair) => (pair && pair[0] && pair[1] ? boxDiff(decodePNG(pair[0]), decodePNG(pair[1]), box, 1) : null);
        console.log(JSON.stringify({ box, vulkan: moved(got.vk), gl: moved(got.gl), files: [...got.vk, ...got.gl] }, null, 2));
      } finally { c.close(); }
      break;
    }
    // [rc4l] `fuactl abperf "<setting>" ...` -- frame cost per console setting inside one session, the A/B that separate launches drift too much for.
    case "abperf": {
      const usage = 'usage: fuactl abperf "<setting>" "<setting>" ... [--rounds N] [--frames N] [--spot NAME | --at x,y,z --face yaw,pitch] [--port P]';
      if (rest.length < 2) { console.error(usage); process.exit(2); }
      const c = await connectTarget(flags, usage);
      try {
        const at = shot.cameraFromFlags(flags, SPOTS);
        if (at) await shot.moveTo(c, at);
        const { verdicts } = await abPerf(c, rest, { rounds: num(flags.rounds) ?? 3, frames: num(flags.frames) ?? 400,
          log: (r) => console.log(`round ${r.round}  ${r.setting.padEnd(36)} p50 ${r.p50.toFixed(3)} ms  p99 ${r.p99.toFixed(3)} ms`) });
        for (const v of verdicts) console.log(`${v.label}: ${v.verdict}, median ${v.median} ms (by round: ${v.deltas.join(", ")})`);
      } finally { c.close(); }
      break;
    }
    // [rc4l] `fuactl costs` -- the step-0 numbers for one view, where they took abperf, three console reports and a script.
    case "costs": {
      const usage = "usage: fuactl costs [--port P] [--spot NAME | --at x,y,z --face yaw,pitch] [--frames N] [--rounds N] [--bench N]";
      const c = await connectTarget(flags, usage);
      try {
        const at = shot.cameraFromFlags(flags, SPOTS);
        if (at) await shot.moveTo(c, at);
        console.log(formatCosts(await measureCosts(c, { frames: num(flags.frames) ?? 400, rounds: num(flags.rounds) ?? 3, bench: num(flags.bench) ?? 120 })));
      } finally { c.close(); }
      break;
    }
    case "ab": {
      const [cvar, a, b] = rest;
      const abUsage = "usage: fuactl ab <cvar> <a> <b> [--spot NAME | --at x,y,z --face yaw,pitch] [--box x0,y0,x1,y1] [--tag T]";
      if (!cvar || a === undefined || b === undefined) { console.error(abUsage); process.exit(2); }
      const c = await connectTarget(flags, abUsage);
      try {
        const files = await shot.frozenAB(c, { cvar, values: [a, b], at: shot.cameraFromFlags(flags, SPOTS),
          outDir: sweepDirFor(engineOf(flags)), tag: flags.tag || `ab_${cvar}` });
        if (files.some((f) => !f)) { console.log(JSON.stringify({ files }, null, 2)); process.exitCode = 1; break; }
        const box = flags.box ? String(flags.box).split(",").map(Number) : [0, 0.05, 1, 1];
        const [A, B, A2] = files.map((f) => decodePNG(f));
        console.log(JSON.stringify({ files, box, ...abDiff(A, B, A2, box) }, null, 2));
      } finally { c.close(); }
      break;
    }
    // [rc4l] `fuactl shot` -- a matched GL/Vulkan pair from an ALREADY RUNNING instance.
    //
    // Every check used to relaunch the engine: a minute and a half of loading pk3s, baking the level
    // and uploading, for a two-second capture. The engine does not need restarting to answer "what
    // does this look like now" -- only to pick up a new binary.
    //
    //   fuactl shot <tag> [--port P --token T] [--spot NAME | --at x,y,z --face yaw,pitch]
    //
    // With no port it uses the running play session. With no camera it captures where the player is
    // standing, which is the case where someone has walked to the thing and wants it recorded.
    case "shot": {
      const tag = rest[0] || "shot";
      const at = shot.cameraFromFlags(flags, SPOTS);
      if (flags.spot) console.error(`spot ${flags.spot}: ${at.shows || ""}`);

      // [rc4l] GL reads its picture back out of the window, so another engine on top makes it black or garbage.
      const engines = engineProcessCount();
      if (engines > 1) console.error(`warning: ${engines} engines are running -- close the others, or the GL capture may be garbage`);
      const c = await connectTarget(flags);
      try {
        const out = await shot.shotPair(c, tag, { at, engineBin: engineOf(flags), sameFrame: !!flags["same-frame"] });
        console.log(JSON.stringify(out, null, 2));
        if (!out.gl || !out.vk) process.exitCode = 1;
      } finally { c.close(); }
      break;
    }
    // [rc4l] `fuactl play` -- a build to walk around in, with the Vulkan view live in the window.
    //
    //   fuactl play [--port P] [--map M] [--iwad W] [--file a.pk3,b.pk3] [--preset ID --variant V]
    //               [--gl] [--side-by-side] [--rt] [--monsters] [--lock]
    //
    // Stays up until Ctrl-C, and writes the port and token to .play-session so a capture taken
    // later can find this instance without depending on a pipe still existing.
    case "play": {
      const sessionFile = SESSION();
      // [rc4l] --restart: quit whatever this session file points at before launching.
      //
      // Every engine change needs the instance replaced, and doing that by hand is where the two
      // instances come from: the old one still holding the port, the new one silently falling back
      // to another, and a capture then taken from whichever answered. One flag, one instance.
      if (flags.restart) {
        const prev = shot.readSession(shot.restartSessionFile(sessionFile, flags.port));
        const outcome = prev && prev.port ? await quitInstance({ port: prev.port, token: prev.token || null }) : "absent";
        if (outcome !== "absent") console.error(`${outcome}: the instance on port ${prev.port}`);
      }
      // [rc4l] A recorded launch's map and wads (found by name in --store or FUACTL_PWADS) into the flags, or exit naming the ones missing.
      const store = presetDirs(flags).storeDir;
      const takeLaunch = (what, sl) => {
        if (sl.missing.length) {
          console.error(`${what} needs ${sl.missing.join(", ")}, which are not in ${store} (set --store or FUACTL_PWADS)`);
          process.exit(2);
        }
        flags.map = flags.map || sl.map;
        flags.file = flags.file || (sl.files.length ? sl.files.join(",") : undefined);
      };
      // [rc4l] --spot: the spot's launch, and the player stood there once it is up.
      let spotAt = null;
      if (flags.spot) {
        takeLaunch(`spot ${flags.spot}`, shot.spotLaunch(SPOTS, String(flags.spot), store));
        spotAt = shot.cameraFromFlags({ spot: flags.spot }, SPOTS);
      }
      // [rc4l] --unpark: what `quit --park` filed for --port, side by side again if it was.
      if (flags.unpark) {
        const park = shot.parkFileFor(sessionFile, Number(flags.port));
        if (!flags.port || !fs.existsSync(park)) { console.error(`nothing parked for port ${flags.port}: run quit --park --port P first`); process.exit(2); }
        const pl = shot.parkedLaunch(park, store);
        takeLaunch("the parked instance", pl);
        flags["side-by-side"] = flags["side-by-side"] || pl.sideBySide;
        flags["no-gl"] = flags["no-gl"] || pl.noGl;
        spotAt = pl.at;
      }
      const inst = await play({
        port: flags.port ? Number(flags.port) : undefined,
        token: flags.token || undefined,
        map: flags.map, iwad: flags.iwad, file: flags.file,
        preset: flags.preset, variant: flags.variant,
        ...presetDirs(flags),
        // [rc4l] --res WxH, so a capture can be framed exactly like the window a report came from.
        ...resFlag(flags.res),
        gl: !!flags.gl, noGl: !!flags["no-gl"] || !!flags.webgpu, webgpu: !!flags.webgpu, sideBySide: !!flags["side-by-side"], rt: !!flags.rt,
        monsters: !!flags.monsters, lock: !!flags.lock,
        extra: shot.parseCvarFlag(flags.cvar) || {},
        at: spotAt,
        sessionFile,
      });
      // [rc4l] The world contract, printed. Each of these has silently invalidated a measurement,
      // and none of them announces itself when it is wrong -- so it is a line here, at launch,
      // rather than something discovered later by the failure it caused.
      for (const ch of inst.world.cheats) {
        console.log(`  ${ch.on ? "on " : "???"} ${ch.command}${ch.flipped ? "   (it was off; flipped back)" : ""}`);
      }
      for (const cv of inst.world.cvars) {
        console.log(`  ${cv.ok ? "ok " : "BAD"} ${cv.name} = ${cv.actual}${cv.ok ? "" : `   (wanted ${cv.expected})`}`);
      }
      console.log(`\n  ready -- the window is showing the ${flags.gl ? "GL" : "VULKAN"} render.`);
      if (!flags.lock) console.log("  mouse and keyboard work normally.");
      console.log(`  fua_vulkan 0 / 1 in the console is a live A/B against GL.\n`);
      console.log(`  PORT=${inst.port} TOKEN=${inst.token}   (also in ${sessionFile})`);
      console.log(`  capture a pair:  fuactl shot <tag> --port ${inst.port} --token ${inst.token}`);
      console.log(`\n  ctrl-C here to quit.`);
      process.on("SIGINT", async () => { await stopInstance(inst); process.exit(0); });
      await new Promise(() => {});
      break;
    }
    // [rc4l] `fuactl shoot` -- fire from a given spot, then photograph the result in both backends.
    //
    // The check this replaces was five manual steps and it was manual every time, so it got run at a
    // slightly different camera each round and the rounds could not be compared. Worse, it kept
    // being re-improvised as a one-off shell script, which is how the same three mistakes -- level
    // not reset, monsters left on, camera forced inside a wall -- each shipped more than once.
    //
    // The point of aiming at a JUNCTION rather than a flat wall is that flat walls have never been
    // the problem: rendering only has to decide anything where two surfaces meet.
    //
    // --look is where the camera is then pointed. It is given rather than discovered, because the
    // shooter cannot know where its own shot landed without the engine telling it, and a tool that
    // guesses that point has photographed the wrong wall before.
    //
    //   fuactl shoot --port P --tag T --at x,y,z --face yaw,pitch [--look x,y,z] [--weapon W] [--map M]
    case "shoot": {
      const shootUsage = "usage: fuactl shoot [--port P] --tag T --at x,y,z --face yaw,pitch [--look x,y,z] [--weapon W] [--map M] [--back N] [--up N] [--after TICS]";
      if (!flags.at || !flags.face) { console.error(shootUsage); process.exit(2); }
      const { x, y, z, angle: yaw, pitch } = shot.cameraFromFlags(flags, SPOTS);
      const weapon = flags.weapon || "RocketLauncher";
      const tag = flags.tag || "shot";
      const c = await connectTarget(flags, shootUsage);
      try {
        await cap.sandbox(c, { map: flags.map || "MAP01" });
        await cap.fire(c, { x, y, z, yaw, pitch, weapon, settleTics: flags.after ? Number(flags.after) : undefined });
        let camera = { x, y, z, yaw, pitch, frac: 1 };
        if (flags.look) {
          const [lx, ly, lz] = String(flags.look).split(",").map(Number);
          const cam = await cap.placeCamera(c, { x: lx, y: ly, z: lz }, yaw, {
            back: flags.back ? Number(flags.back) : undefined,
            up: flags.up ? Number(flags.up) : undefined,
          });
          if (!cam) { console.log(JSON.stringify({ tag, weapon, camera: null })); break; }
          camera = cam;
          await cap.waitTics(c, 6);
        }
        // shotPair, not a second capture path: it PAUSES the sim between the two halves, which is
        // the only way anything transient appears in both.
        const shots = await shot.shotPair(c, tag, { engineBin: engineOf(flags) });
        console.log(JSON.stringify({ tag, weapon, camera, ...shots }, null, 2));
      } finally { c.close(); }
      break;
    }
    case "renderer-info": {
      const c = await connectTarget(flags, "usage: fuactl renderer-info [--port P --token T]");
      const info = await c.rpc("renderer.info");
      c.close();
      console.log(JSON.stringify(info, null, 2));
      break;
    }
    case "net-bw": {
      const report = await runNetBandwidth({
        seed: flags.seed ? Number(flags.seed) : undefined,
        map: flags.map || undefined,
        spawn: flags.spawn || undefined,
        count: flags.count ? Number(flags.count) : undefined,
        seconds: flags.seconds ? Number(flags.seconds) : undefined,
        log: (m) => console.error(`[net-bw] ${m}`),
      });
      console.log(JSON.stringify(report, null, 2));
      break;
    }
    case "ui": {
      // Drive the UI of a running instance: fuactl ui <action> [args] --port P [--token T]
      const [act, ...a] = rest;
      const r = await withUi(flags, async (c) => {
        switch (act) {
          case "nav":        return ui.menuNav(c, a).then(() => ({ navigated: a }));
          case "key":        return ui.menuNav(c, a).then(() => ({ keys: a })); // alias
          case "click":
            // `ui click <x> <y>` clicks a point; `ui click <label...>` finds the label and clicks its centre.
            if (a.length && Number.isNaN(Number(a[0]))) return ui.clickLabel(c, a.join(" "), { button: flags.button || "left", double: !!flags.double });
            return ui.click(c, Number(a[0]), Number(a[1]), { button: flags.button || "left", double: !!flags.double }).then(() => ({ clicked: [Number(a[0]), Number(a[1])], button: flags.button || "left" }));
          case "rightclick": return ui.rightClick(c, Number(a[0]), Number(a[1])).then(() => ({ rightClicked: [Number(a[0]), Number(a[1])] }));
          case "drag":       return ui.drag(c, Number(a[0]), Number(a[1]), Number(a[2]), Number(a[3])).then(() => ({ dragged: a.map(Number) }));
          case "type":       return ui.typeText(c, a.join(" ")).then(() => ({ typed: a.join(" ") }));
          case "look":       return ui.look(c, { yaw: flags.yaw ? Number(flags.yaw) : 0, pitch: flags.pitch ? Number(flags.pitch) : 0 });
          case "stick":      return c.rpc("input.axis", flags.clear ? { clear: true } : { yaw: num(flags.yaw), pitch: num(flags.pitch), forward: num(flags.forward), side: num(flags.side) });
          case "screenshot": return ui.screenshot(c, engineOf(flags), a[0] || "fuactl_shot").then((s) => ({ path: s.path, bytes: s.base64.length }));
          case "read":       return ui.readMenu(c).then((m) => (flags.full ? m : { lines: m.lines.map((l) => l.text) }));
          case "find":       return ui.findLabel(c, a.join(" "));
          case "warp":       return ui.warp(c, Number(a[0]), Number(a[1]));
          case "damaging":   return ui.damagingSectors(c, flags.limit ? Number(flags.limit) : 64);
          // [rc4l] exec RETURNS WHAT THE COMMAND PRINTED. console.exec answers {"executed":true}
          // and drops the output, so every stats dump driven through here came back empty and had to
          // be read off the log file by guessing which lines were new -- the exact guess capture.exec
          // was written to remove. Listen for the tee instead.
          case "exec":       process.stdout.write(await cap.exec(c, a.join(" "), { quietMs: 500, maxMs: 20000 })); return null;
          default: throw new Error(`unknown ui action: ${act} (nav/click/rightclick/drag/type/look/stick/screenshot/exec)`);
        }
      });
      // exec has already written the command's own output; a bare "null" under it is noise.
      if (r !== null && r !== undefined) console.log(JSON.stringify(r, null, 2));
      break;
    }
    case "browser": {
      // End-to-end check of server discovery: refresh both paths, wait for replies, and report
      // what the browser actually holds. --expect-lan / --expect-country turn it into an
      // assertion that exits non-zero, so it can gate a release the same way a test would.
      if (!flags.port) { console.error("usage: fuactl browser --port P [--token T] [--wait S] [--expect-lan] [--expect-country USA]"); process.exit(2); }
      const waitS = flags.wait != null ? Number(flags.wait) : 12;
      const c = new BridgeClient();
      await c.connect(Number(flags.port), { token: flags.token || null });
      await c.waitHello();
      await c.rpc("browser.refresh");
      let list = { servers: [], count: 0 };
      const deadline = Date.now() + waitS * 1000;
      // Poll rather than sleep once: LAN replies land in milliseconds, registry ones take a round trip.
      while (Date.now() < deadline) {
        await new Promise((r) => setTimeout(r, 1000));
        list = await c.rpc("browser.list", {}, 15000);
        if (list.count > 0 && Date.now() > deadline - (waitS - 4) * 1000) break;
      }
      c.close();
      const lan = list.servers.filter((x) => x.lan);
      const net = list.servers.filter((x) => !x.lan);
      console.log(JSON.stringify({ count: list.count, lan: lan.length, internet: net.length, servers: list.servers }, null, 2));
      let failed = false;
      if (flags["expect-lan"] && lan.length === 0) { console.error("FAIL: expected at least one LAN server, saw none"); failed = true; }
      if (flags["expect-country"]) {
        const want = String(flags["expect-country"]).toUpperCase();
        // Match either the code the server reported or the flag the browser resolved, since a
        // server whose own lookup failed reports nothing and the client geolocates it instead.
        const matches = (x) => [x.country, x.flag].some((v) => (v || "").toUpperCase() === want);
        if (!net.some(matches)) {
          console.error(`FAIL: expected a non-LAN server with country/flag ${want}; saw ${JSON.stringify(net.map((x) => x.flag || x.country || null))}`);
          failed = true;
        }
      }
      if (failed) process.exit(1);
      break;
    }
    case "continue": {
      // The Continue button's decision, reported rather than eyeballed. `shown` and the record are
      // separate facts on purpose: a perfectly good record is still hidden while a game is running,
      // and an E2E that only checked one of them would pass on the wrong reason.
      if (!flags.port) { console.error("usage: fuactl continue --port P [--token T] [--expect shown|hidden]"); process.exit(2); }
      const c = new BridgeClient();
      await c.connect(Number(flags.port), { token: flags.token || null });
      await c.waitHello();
      const info = await c.rpc("ui.continue");
      c.close();
      console.log(JSON.stringify(info, null, 2));
      if (flags.expect) {
        const want = String(flags.expect) === "shown";
        if (info.shown !== want) {
          console.error(`expected the button ${want ? "shown" : "hidden"}, it was ${info.shown ? "shown" : "hidden"}`);
          process.exit(1);
        }
      }
      break;
    }

    case "hostdiag": {
      // "Is my server visible to anyone else?" answered by the only witness that counts, the
      // registry. Distinct from `browser`, which reports what the local browser BELIEVES -- a host's
      // own public row is fabricated from its LAN row, so it lights up either way and cannot
      // distinguish a dead port forward from a router that will not hairpin.
      //
      // --wait polls, because verification arrives on the registry's schedule (announce every 30s),
      // so the honest answer to "is it reachable" is simply not available in the first second.
      if (!flags.port) { console.error("usage: fuactl hostdiag --port P [--token T] [--wait S] [--expect-listed]"); process.exit(2); }
      const waitS = flags.wait != null ? Number(flags.wait) : 0;
      const c = new BridgeClient();
      await c.connect(Number(flags.port), { token: flags.token || null });
      await c.waitHello();
      let diag = await c.rpc("net.hostdiag");
      const deadline = Date.now() + waitS * 1000;
      while (!diag.registryReplied && Date.now() < deadline) {
        await new Promise((r) => setTimeout(r, 2000));
        diag = await c.rpc("net.hostdiag");
      }
      c.close();
      console.log(JSON.stringify(diag, null, 2));
      if (!diag.hosting) console.error("note: not hosting, so there is nothing to be listed as");
      else if (diag.registryReplied) {
        // Deliberately not "you are reachable". The registry's reply comes back through the mapping
        // this server's own announce opened, so it arrives from behind a closed port too.
        console.error("the registry is listing this server and can talk to it; that is NOT proof players can join");
      }
      if (flags["expect-listed"] && !diag.registryReplied) {
        console.error("FAIL: expected the registry to be answering this server, it is not");
        process.exit(1);
      }
      break;
    }
    case "mcp": {
      const { runMcpServer } = await import("./mcp.mjs");
      await runMcpServer();
      break;
    }
    default:
      console.log(USAGE);
      process.exit(cmd ? 1 : 0);
  }
}

main().catch((e) => { console.error("fuactl error:", e.message); process.exit(1); });
