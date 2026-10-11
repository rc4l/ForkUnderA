#!/usr/bin/env node
// fuactl -- ForkUnderA companion. One tool, two faces:
//   fuactl <cmd>     humans / CI          fuactl mcp     agents (MCP stdio server)
// Both talk to the engine's native bridge (features/mcp-bridge), which must be built with
// -DFUA_MCP_BRIDGE=ON (ZX_MCP_BRIDGE=1 ./mac_compile.sh) and armed with ZANDRONUM_BRIDGE_PORT.
import { reap, readRegistry } from "./registry.mjs";
import { runDeterminismCheck, runPerfAblation, runNetBandwidth, runGlTimers } from "./session.mjs";
import { launchInstance, stopInstance, resolveEngine, engineProcessCount } from "./launch.mjs";
import { BridgeClient } from "./client.mjs";
import { diligentRun } from "./diligent.mjs";
import { sampleProcess } from "./sample.mjs";
import { summarizeGlTimers } from "./proto.mjs";
import * as ui from "./ui.mjs";
import { runBench } from "./bench.mjs";
import * as cap from "./capture.mjs";
import { play, quitInstance, defaultPwadStore, resolvePreset } from "./play.mjs";
import { loadQueue, saveQueue, nextId, spotNameFor, addItem, noteItem, closeItem, nextOpen, formatItem } from "./queue.mjs";
import { findMapLumps, mapReport } from "./mapdata.mjs";
import * as shot from "./shot.mjs";
import { sweepDirFor } from "./shot.mjs";
import * as sweepMod from "./sweep.mjs";
import { png } from "./png.mjs";
import { actorsNear } from "./actors.mjs";
import { pixelToFace } from "./camera.mjs";
import { sectorOf } from "./sectorof.mjs";
import { scenarioOptions, scenarioRun, templateScenarioFiles, probeScenario } from "./template.mjs";
import { decodePNG, abDiff, boxDiff } from "./pngcore.mjs";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { usageFor } from "./usage.mjs";

const num = (v) => (v != null && v !== true ? Number(v) : undefined);

const SPOTS = new URL("../spots.json", import.meta.url);
const SESSION = () => path.resolve(process.cwd(), ".play-session");

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
  const db = fs.existsSync(file) ? JSON.parse(fs.readFileSync(file, "utf8")) : { spots: {} };
  db.spots[name] = Object.assign({}, db.spots[name], {
    map: cam.map, x: cam.x, y: cam.y, z: cam.z, angle: cam.yaw, pitch: cam.pitch,
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
  launch [--map M] [--seed S] [--port P] [--token T] [--iwad W] [--skill N] [--file a.wad,b.pk3] [--cvar k=v,k2=v2] [--play]   launch one supervised bridge instance (stays up until Ctrl-C)
  sample --pid P | --port P [--seconds N] [--engine]   hottest functions (macOS sample / Linux perf; unavailable on Windows)
  net-bw [--seed S] [--map M] [--spawn CLS] [--count N] [--seconds N]   client/server bandwidth, baseline vs perturbation
  cycle --port P [--token T] [--count N] [--settle MS]   advance the map rotation N times, confirming intermissions
  rpc <cmd> [jsonArgs] --port P [--token T]   send one RPC to an instance and print the result
  queue add <description> [--port P] [--name SPOT]   save the instance's vantage as a spot and queue the problem seen there (report-queue skill)
  queue list [--all] | next | note <id> <text> | done <id> [--unverified] [--note TEXT]   read and work the queue, oldest open item first
  burst <tag> [--side gl|vk] [--count N] [--box x0,y0,x1,y1] [--spot S | --at --face] [--port P]   timed captures with the sim running and each one's box brightness, for an animation's rate or a light a paused shot hides
  sector-of --pixel x,y [--side gl|vk] [--spot S | --at --face] [--port P]   which sector's colour that pixel of a capture follows, found by painting halves of the map red and green (fua_sector_color); the sectors stay painted
  travel --to MAP [--max N] [--port P]   nextmap until the instance is on MAP, the way a player reached it
  quit [--port P] [--token T] [--park]   quit an instance (else the last play session) and wait until its port closes; exit 1 if it would not go; --park files its camera, wads and renderer first
  session [--instances N] [--seed S] [--map M] [--tics T]   run the determinism + desync check
  perf-ab [--seed S] [--map M] [--spawn CLS] [--count N] [--frames F]   deterministic perf ablation (baseline vs perturbation, causal ms delta + sim/render verdict)
  gl-timers --port P [--token T] [--frames N] [--warmup M]   GPU render profiling: per-pass GPU ms (scene/translucent/hud2d) of a running instance
  capture --port P [--frames N] [--warmup M]   frametime distribution (p50/p95/p99/max, sim vs render split)
  ticprof --port P [--tics N]          per-tic sim phase split (P_Ticker / thinkers / effects / specials)
  bench --port P --scenario F.json [--runs N] [--metric total.p99_ms]   repeat a scenario, report median + spread, discard runs whose expectations failed
  lines --port P [--special N] [--door] [--use] [--cross] [--tag N] [--texture NAME] [--limit N]   query linedefs; prints a stand position and facing for each match
  here --port P [--token T] [--save NAME] [--note TEXT]   the live camera (position + facing, full precision) and what the crosshair is on; --save records it in spots.json as a named repro
  renderer-info --port P [--token T]   renderer identity + whether GL timer queries work on this driver
  ui <action> [args] --port P [--token T]   drive the UI: read (menu as text), find <label>, nav <keys>, click <x> <y>, drag, type <text>, look --yaw D --pitch D, screenshot [name], exec <ccmd>
  diligent --port P [--frames N] [--shot FILE] [--sweep DIR]   drive the Diligent (Vulkan) backend: bake the level mesh, upload geometry, optional swapchain screenshot, optional debug-view sweep (lm0..lm4.png in DIR), the matched Diligent-vs-GL benchmark, and with --scale a GPU-time probe at 1x..100x the visible geometry
  play [--restart] [--port P] [--token T] [--spot NAME | --unpark] [--map M] [--file a.pk3,b.pk3] [--cvar k=v,k2=v2] [--preset ID --variant V --store DIR] [--gl] [--side-by-side] [--rt] [--monsters] [--lock] [--res WxH]   a build to walk around in, Vulkan live in the window; stays up until Ctrl-C
  build [--root DIR] [--no-stage] [--tests]   compile the engine (with --tests the unit tests too, and run them) and stage it unless --no-stage, failing loudly instead of staging a stale binary
  mapdata <MAP> (--preset ID | --file a.pk3,b.wad) [--sector N,..] [--line N,..] [--special N] [--tag N] [--scripts REGEX]   a UDMF level's summary, sectors, lines and ACS source lines, read from its files
  actors [--near x,y] [--radius R] [--toward YAW | --ahead] [--cone DEG] [--class REGEX]   actors near a point (default the player), nearest first, optionally only in a direction
  probe <scenario.mjs> [--map M] [--vantage V] [--side S] [--engine path/to/forkundera.exe] [<command> ...]   launch a template scenario's map at a vantage and run console commands there
  scenarios [name ...] [--engine path/to/forkundera.exe]   run every template scenario (or the named ones) in turn and summarise; --engine picks the binary, as FUACTL_ENGINE does
  ready [--port P --token T] [--timeout S]   wait until an instance answers (defaults to the last play session)
  goto (--spot NAME | --at x,y,z --face yaw,pitch) [--port P]   stand the player there
  motion [--spot NAME | --at x,y,z --face yaw,pitch] [--box x0,y0,x1,y1] [--interval MS]   per-pixel change between two captures apart, Vulkan and GL, sim running
  ab <cvar> <a> <b> [--spot NAME | --at x,y,z --face yaw,pitch] [--box x0,y0,x1,y1]   Vulkan under each value in ONE paused frame, plus a repeat as the noise floor
  shot <tag> [--port P] [--spot NAME | --at x,y,z --face yaw,pitch] [--force]   matched GL/Vulkan pair from a running instance, one camera, sim frozen
  shoot [--port P] --tag T --at x,y,z --face yaw,pitch [--look x,y,z] [--weapon W] [--map M] [--after TICS]   fire at a junction and capture a GL/Vulkan pair (--look frames a point, --after catches transient effects before they fade)
  sweep [--maps "MAP01 MAP07"] [--port P]   matched pairs across several maps, ranked by how much the renderers disagree
  doorshot <tag> [--port P] [--at x,y,z --face yaw] [--mid TICS]   a door caught MID-SWING in both renderers, plus a before pair
  look [--port P] [--spot S | --at x,y,z --face yaw,pitch] [--pixel x,y [--size WxH]] [--stack]   what the crosshair is on and what the level mesh holds for it; --pixel aims through that pixel of a capture from the camera, --stack casts through the first stacked view
  line <index> | --at X,Y [--radius R]   what a linedef IS: blocking flags, front/back sector heights, and which tier carries a texture over which span of height -- the three things that decide whether a mark can exist there. --at finds every line near a point, which is what tells two lines in the same place apart.
  png <mode> ...                     pixel arithmetic on captures: --diff, --boxdiff, --diffimg, --align, --crop, --rows, --blob, --px, --range (a box's brightness and channel means, over several files)
  mcp                                run as an MCP stdio server for agents
`;

async function main() {
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
        // on a 1920x1200 window could only be reproduced at a third of the pixels -- where it is
        // milder, and where anything fill-bound hides entirely. `play` had --res; the measuring
        // command, which is the one that needs it, did not.
        ...(flags.res ? (() => {
          const m = String(flags.res).match(/^(\d+)x(\d+)$/i);
          if (!m) throw new Error(`--res wants WxH, e.g. 1920x1200 (got ${flags.res})`);
          return { width: Number(m[1]), height: Number(m[2]) };
        })() : {}),
        // [rc4l] PWADs, comma-separated. Without this the only launchable thing was a bare IWAD, so
        // a mod could be profiled only by hosting a server first and measuring through the netcode.
        extraArgs: flags.file
          ? String(flags.file).split(",").flatMap((f) => ["-file", f.trim()])
          : undefined,
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
      console.log(`engine ${resolveEngine()}`);
      let failed = 0;
      for (const f of files) {
        const url = pathToFileURL(path.join(dir, f)).href;
        await import(url);
        const run = scenarioRun(url);
        if (!run) continue;
        const out = await run();
        for (const l of out.lines.filter((x) => !x.startsWith("PASS"))) console.log(`${f.slice(0, -4).padEnd(18)} ${l}`);
        if (out.code !== 0) failed++;
      }
      console.log(failed ? `${failed} scenario(s) failed` : "every scenario within its limits");
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
        engineBin: engineOf(flags).split(path.sep).join("/"), tag: rest[0] || "burst",
      });
      for (const r of out) console.log(`${r.i} t=${r.t}ms mean=${r.mean.toFixed(2)} ${r.file || "(no capture)"}`);
      c.close();
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
          engineBin: engineOf(flags).split(path.sep).join("/"), log: (m) => console.error(m) });
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
        const c = await connectTarget(flags, usage);
        const cam = await captureSpot(c, flags.name ? String(flags.name) : spotNameFor(nextId(q), description), description);
        c.close();
        const r = addItem(q, { description, spot: flags.name ? String(flags.name) : undefined, map: cam.map,
          at: { x: cam.x, y: cam.y, z: cam.z, angle: cam.yaw, pitch: cam.pitch } });
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
      const outcome = await quitInstance(t);
      console.log(`${outcome}: port ${t.port}`);
      process.exitCode = outcome === "running" ? 1 : 0;
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
        const e = readRegistry().find((x) => String(x.port) === String(flags.port));
        if (e) pid = e.pid;
      }
      if (!pid) { console.error("usage: fuactl sample --pid P | --port P [--seconds N]"); process.exit(2); }
      const opts = {
        seconds: flags.seconds ? Number(flags.seconds) : 2,
        top: flags.top ? Number(flags.top) : 12,
        engineOnly: !!flags.engine,
      };
      // [rc4l] The Windows backend lives inside the engine, so it needs a connection rather than a
      // pid. Opened only when --port was given; the external backends never touch it.
      const r = flags.port
        ? await withUi(flags, (c) => sampleProcess(pid, { ...opts, conn: c }))
        : await sampleProcess(pid, opts);
      console.log(JSON.stringify(r, null, 2));
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
      const report = await withUi(flags, async (c) => {
        const done = c.waitEvent("perf", 120000);
        await c.rpc("perf.capture", { frames: num(flags.frames) ?? 300, warmup: num(flags.warmup) ?? 0 });
        return done;
      });
      console.error(`[capture] p50=${report.total.p50_ms}ms p99=${report.total.p99_ms}ms max=${report.total.max_ms}ms (sim ${report.sim_mean_ms.toFixed(2)} / render ${report.render_mean_ms.toFixed(2)})`);
      console.log(JSON.stringify(report, null, 2));
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
      console.error("[diligent] done -- results are in the engine log (fua_diligent_bench / fua_gl_meshbench lines)");
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

      // launchInstance writes to <tmp>/fuactl-XXXX/engine-<port>.log; the registry does not record
      // the path, so find the newest one for this port.
      const logPath = (() => {
        const tmp = os.tmpdir();
        let best = null, bestTime = -1;
        for (const d of fs.readdirSync(tmp)) {
          if (!d.startsWith("fuactl-")) continue;
          const p = path.join(tmp, d, `engine-${flags.port}.log`);
          try {
            const st = fs.statSync(p);
            if (st.mtimeMs > bestTime) { bestTime = st.mtimeMs; best = p; }
          } catch { /* not this one */ }
        }
        return best;
      })();
      const before = logPath && fs.existsSync(logPath) ? fs.statSync(logPath).size : 0;

      await withUi(flags, (c) => c.rpc("console.exec", { text: `fua_find_lines ${parts.join(" ")}` }));
      await new Promise((r) => setTimeout(r, 1200));

      if (!logPath || !fs.existsSync(logPath)) {
        console.error("no log for that instance; run the CCMD directly and read its console");
        break;
      }
      const fd = fs.openSync(logPath, "r");
      const buf = Buffer.alloc(Math.max(0, fs.statSync(logPath).size - before));
      fs.readSync(fd, buf, 0, buf.length, before);
      fs.closeSync(fd);
      for (const l of buf.toString("utf8").split(/\r?\n/)) {
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
        // [rc4l] --pixel turns a point on a capture into the facing that puts the crosshair on it, instead of a hand-worked angle.
        if (flags.pixel && cam) {
          const [px, py] = String(flags.pixel).split(",").map(Number);
          const [w, h] = String(flags.size || "1024x640").split("x").map(Number);
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
    // [rc4l] `fuactl mapdata <MAP>` -- a level's sectors, lines, specials and ACS source read from its files, no engine needed.
    case "mapdata": {
      const map = rest[0];
      const mdUsage = "usage: fuactl mapdata <MAP> (--preset ID [--variant V --store DIR] | --file a.pk3,b.wad) [--sector N,..] [--line N,..] [--special N] [--tag N] [--scripts REGEX]";
      if (!map || (!flags.preset && !flags.file)) { console.error(mdUsage); process.exit(2); }
      const { catalogueDir, storeDir } = presetDirs(flags);
      const paths = flags.preset ? resolvePreset(catalogueDir, storeDir, flags.preset, flags.variant)
        : String(flags.file).split(",").map((f) => f.trim()).filter(Boolean);
      const found = findMapLumps(paths.map((p) => ({ name: path.basename(p), buf: fs.readFileSync(p) })), map);
      if (!found) { console.error(`${map} is in none of: ${paths.map((p) => path.basename(p)).join(", ")}`); process.exit(1); }
      const list = (v) => (v ? String(v).split(",").map(Number) : []);
      console.log(JSON.stringify(mapReport(found, { sectors: list(flags.sector), lines: list(flags.line),
        scripts: flags.scripts ? String(flags.scripts) : null, special: flags.special ? Number(flags.special) : null,
        tag: flags.tag !== undefined ? Number(flags.tag) : null }), null, 2));
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
        const out = await shot.shotPair(c, tag, { at, engineBin: engineOf(flags) });
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
        const prev = shot.readSession(sessionFile);
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
        spotAt = pl.at;
      }
      const inst = await play({
        port: flags.port ? Number(flags.port) : undefined,
        token: flags.token || undefined,
        map: flags.map, iwad: flags.iwad, file: flags.file,
        preset: flags.preset, variant: flags.variant,
        ...presetDirs(flags),
        // [rc4l] --res WxH, so a capture can be framed exactly like the window a report came from.
        width: flags.res ? Number(String(flags.res).split("x")[0]) : undefined,
        height: flags.res ? Number(String(flags.res).split("x")[1]) : undefined,
        gl: !!flags.gl, sideBySide: !!flags["side-by-side"], rt: !!flags.rt,
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
      if (!flags.lock) console.log("  mouse and keyboard work normally. walking into unbaked areas bakes them in as you go.");
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
