#!/usr/bin/env node
// fuactl -- ForkUnderA companion. One tool, two faces:
//   fuactl <cmd>     humans / CI          fuactl mcp     agents (MCP stdio server)
// Both talk to the engine's native bridge (features/mcp-bridge), which must be built with
// -DFUA_MCP_BRIDGE=ON (ZX_MCP_BRIDGE=1 ./mac_compile.sh) and armed with ZANDRONUM_BRIDGE_PORT.
import { reap, readRegistry } from "./registry.mjs";
import { runDeterminismCheck, runPerfAblation, runNetBandwidth, runGlTimers } from "./session.mjs";
import { launchInstance, stopInstance, resolveEngine } from "./launch.mjs";
import { BridgeClient } from "./client.mjs";
import { diligentRun } from "./diligent.mjs";
import { sampleProcess } from "./sample.mjs";
import { summarizeGlTimers } from "./proto.mjs";
import * as ui from "./ui.mjs";
import { runBench } from "./bench.mjs";
import * as cap from "./capture.mjs";
import { play } from "./play.mjs";
import * as shot from "./shot.mjs";
import * as sweepMod from "./sweep.mjs";
import { png } from "./png.mjs";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const num = (v) => (v != null && v !== true ? Number(v) : undefined);

// Connect to an instance, run fn with the client, print nothing extra, then close.
async function withUi(flags, fn) {
  if (!flags.port) { console.error("ui needs --port P [--token T]"); process.exit(2); }
  const c = new BridgeClient();
  await c.connect(Number(flags.port), { token: flags.token || null });
  await c.waitHello();
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
  session [--instances N] [--seed S] [--map M] [--tics T]   run the determinism + desync check
  perf-ab [--seed S] [--map M] [--spawn CLS] [--count N] [--frames F]   deterministic perf ablation (baseline vs perturbation, causal ms delta + sim/render verdict)
  gl-timers --port P [--token T] [--frames N] [--warmup M]   GPU render profiling: per-pass GPU ms (scene/translucent/hud2d) of a running instance
  capture --port P [--frames N] [--warmup M]   frametime distribution (p50/p95/p99/max, sim vs render split)
  ticprof --port P [--tics N]          per-tic sim phase split (P_Ticker / thinkers / effects / specials)
  bench --port P --scenario F.json [--runs N] [--metric total.p99_ms]   repeat a scenario, report median + spread, discard runs whose expectations failed
  lines --port P [--special N] [--door] [--use] [--cross] [--tag N] [--limit N]   query linedefs; prints a stand position and facing for each match
  here --port P [--token T] [--save NAME] [--note TEXT]   the live camera (position + facing, full precision) and what the crosshair is on; --save records it in spots.json as a named repro
  renderer-info --port P [--token T]   renderer identity + whether GL timer queries work on this driver
  ui <action> [args] --port P [--token T]   drive the UI: read (menu as text), find <label>, nav <keys>, click <x> <y>, drag, type <text>, look --yaw D --pitch D, screenshot [name], exec <ccmd>
  diligent --port P [--frames N] [--shot FILE] [--sweep DIR]   drive the Diligent (Vulkan) backend: bake the level mesh, upload geometry, optional swapchain screenshot, optional debug-view sweep (lm0..lm4.png in DIR), the matched Diligent-vs-GL benchmark, and with --scale a GPU-time probe at 1x..100x the visible geometry
  play [--port P] [--map M] [--file a.pk3,b.pk3] [--preset ID --variant V] [--gl] [--side-by-side] [--rt] [--monsters] [--lock]   a build to walk around in, Vulkan live in the window; stays up until Ctrl-C
  build [--root DIR]                 compile the engine and stage it, failing loudly instead of staging a stale binary
  shot <tag> [--port P] [--spot NAME | --at x,y,z --face yaw,pitch]   matched GL/Vulkan pair from a running instance, one camera, sim frozen
  mark --port P --tag T --at x,y,z --face yaw,pitch [--weapon W] [--map M] [--after TICS]   fire at a junction, find the mark, and capture a GL/Vulkan pair of it (--after catches transient decals before they fade)
  sweep [--maps "MAP01 MAP07"] [--port P]   matched pairs across several maps, ranked by how much the renderers disagree
  doorshot <tag> [--port P] [--at x,y,z --face yaw] [--mid TICS]   a door caught MID-SWING in both renderers, plus a before pair
  look [--port P] [--at x,y,z --face yaw,pitch]   what the crosshair is on and what the level mesh holds for it
  png <mode> ...                     pixel arithmetic on captures: --diff, --diffimg, --align, --crop, --rows, --blob
  mcp                                run as an MCP stdio server for agents
`;

async function main() {
  const [cmd, ...argv] = process.argv.slice(2);
  const { flags, rest } = parseFlags(argv);

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
        // [rc4l] PWADs, comma-separated. Without this the only launchable thing was a bare IWAD, so
        // a mod could be profiled only by hosting a server first and measuring through the netcode.
        extraArgs: flags.file
          ? String(flags.file).split(",").flatMap((f) => ["-file", f.trim()])
          : undefined,
        // [rc4l] --cvar name=value, repeatable as a comma-separated list. Applied before the map
        // loads, which is the only time some of them take effect (sv_nomonsters being the one that
        // matters for hands-off testing).
        cvars: flags.cvar
          ? Object.fromEntries(String(flags.cvar).split(",").map((kv) => {
              const i = kv.indexOf("=");
              return i < 0 ? [kv.trim(), "1"] : [kv.slice(0, i).trim(), kv.slice(i + 1).trim()];
            }))
          : undefined,
      });
      console.log(`launched pid=${inst.pid} port=${inst.port} token=${inst.token}`);
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
    case "rpc": {
      const rpcCmd = rest[0];
      if (!rpcCmd || !flags.port) { console.error("usage: fuactl rpc <cmd> [jsonArgs] --port P [--token T]"); process.exit(2); }
      const args = rest[1] ? JSON.parse(rest[1]) : undefined;
      const c = new BridgeClient();
      await c.connect(Number(flags.port), { token: flags.token || null });
      await c.waitHello();
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
                      " [--cross] [--tag N] [--limit N]");
        process.exit(2);
      }
      const parts = [];
      if (flags.special != null && flags.special !== true) parts.push(`special=${flags.special}`);
      if (flags.tag != null && flags.tag !== true) parts.push(`tag=${flags.tag}`);
      if (flags.door) parts.push("door=1");
      if (flags.use) parts.push("use=1");
      if (flags.cross) parts.push("cross=1");
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
      if (!flags.port) { console.error("usage: fuactl here --port P [--token T] [--save NAME] [--note TEXT]"); process.exit(2); }
      const c = new BridgeClient();
      await c.connect(Number(flags.port), { token: flags.token || null });
      await c.waitHello();
      const cam = await c.rpc("player.camera");
      c.close();
      if (flags.save) {
        const p = new URL("../spots.json", import.meta.url);
        const db = JSON.parse(fs.readFileSync(p, "utf8"));
        db.spots[flags.save] = Object.assign({}, db.spots[flags.save], {
          map: cam.map, x: cam.x, y: cam.y, z: cam.z, angle: cam.yaw, pitch: cam.pitch,
          shows: flags.note || (db.spots[flags.save] && db.spots[flags.save].shows) ||
                 "recorded from a live session",
        });
        fs.writeFileSync(p, JSON.stringify(db, null, 2) + "\n");
        console.error(`saved spot ${flags.save}`);
      }
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
    case "look": {
      const session = (!flags.port && shot.readSession(path.resolve(process.cwd(), ".play-session"))) || null;
      const port = Number(flags.port || (session && session.port));
      if (!port) { console.error("no --port and no .play-session -- nothing running"); process.exit(2); }
      const c = new BridgeClient();
      await c.connect(port, { token: flags.token || (session && session.token) || null });
      await c.waitHello();
      try {
        let at = null;
        if (flags.at) {
          const [x, y, z] = String(flags.at).split(",").map(Number);
          const [yaw, pitch] = String(flags.face || "0,0").split(",").map(Number);
          at = { x, y, z, angle: yaw, pitch };
        }
        process.stdout.write(await sweepMod.look(c, at));
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
      const session = (!flags.port && shot.readSession(path.resolve(process.cwd(), ".play-session"))) || null;
      const port = Number(flags.port || (session && session.port));
      if (!port) { console.error("no --port and no .play-session -- nothing running"); process.exit(2); }
      const c = new BridgeClient();
      await c.connect(port, { token: flags.token || (session && session.token) || null });
      await c.waitHello();
      try {
        let at;
        if (flags.at) {
          const [x, y, z] = String(flags.at).split(",").map(Number);
          at = { x, y, z, angle: Number(String(flags.face || "90,0").split(",")[0]) };
        }
        const out = await sweepMod.doorShot(c, rest[0] || "door", {
          at, engineBin: resolveEngine(),
          midTics: flags.mid ? Number(flags.mid) : undefined,
        });
        console.log(JSON.stringify(out, null, 2));
      } finally { c.close(); }
      break;
    }
    // [rc4l] `fuactl build` -- compile and stage, failing loudly rather than staging nothing.
    case "build": {
      const r = shot.build({ root: flags.root || undefined });
      const when = new Date(fs.statSync(r.staged).mtime).toTimeString().slice(0, 8);
      console.log(`build ok, staged ${when} (+${r.entries} catalogue entries)`);
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
      const session = (!flags.port && shot.readSession(path.resolve(process.cwd(), ".play-session"))) || null;
      const port = Number(flags.port || (session && session.port));
      const token = flags.token || (session && session.token) || null;
      if (!port) { console.error("no --port and no .play-session -- nothing running"); process.exit(2); }

      let at = null;
      if (flags.spot) {
        at = shot.readSpot(new URL("../spots.json", import.meta.url), String(flags.spot));
        console.error(`spot ${flags.spot}: ${at.shows || ""}`);
      } else if (flags.at) {
        const [x, y, z] = String(flags.at).split(",").map(Number);
        const [yaw, pitch] = String(flags.face || "0,0").split(",").map(Number);
        at = { x, y, z, angle: yaw, pitch };
      }

      const c = new BridgeClient();
      await c.connect(port, { token });
      await c.waitHello();
      try {
        const out = await shot.shotPair(c, tag, { at, engineBin: flags.engine || resolveEngine() });
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
      const sessionFile = path.resolve(process.cwd(), ".play-session");
      const inst = await play({
        port: flags.port ? Number(flags.port) : undefined,
        map: flags.map, iwad: flags.iwad, file: flags.file,
        preset: flags.preset, variant: flags.variant,
        catalogueDir: path.resolve(fileURLToPath(new URL(".", import.meta.url)), "../../../catalogue"),
        storeDir: path.join(process.env.LOCALAPPDATA || os.tmpdir(), "ForkUnderA/pwads"),
        gl: !!flags.gl, sideBySide: !!flags["side-by-side"], rt: !!flags.rt,
        monsters: !!flags.monsters, lock: !!flags.lock,
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
    // [rc4l] `fuactl mark` -- shoot something, then look at what it left.
    //
    // The check this replaces was five manual steps and it was manual every time, so it got run at a
    // slightly different camera each round and the rounds could not be compared. Worse, it kept
    // being re-improvised as a one-off shell script, which is how the same three mistakes -- level
    // not reset, monsters left on, camera forced inside a wall -- each shipped more than once.
    //
    // The point of aiming at a JUNCTION rather than a flat wall is that flat walls have never been
    // the problem: a decal only has to decide anything where two surfaces meet.
    //
    //   fuactl mark --port P --tag T --at x,y,z --face yaw,pitch [--weapon W] [--map M]
    case "mark": {
      if (!flags.port || !flags.at || !flags.face) {
        console.error("usage: fuactl mark --port P --tag T --at x,y,z --face yaw,pitch [--weapon W] [--map M] [--back N] [--up N]");
        process.exit(2);
      }
      const [x, y, z] = String(flags.at).split(",").map(Number);
      const [yaw, pitch] = String(flags.face).split(",").map(Number);
      const weapon = flags.weapon || "RocketLauncher";
      const tag = flags.tag || "mark";
      const c = new BridgeClient();
      await c.connect(Number(flags.port), { token: flags.token || null });
      await c.waitHello();
      try {
        await cap.sandbox(c, { map: flags.map || "MAP01" });
        await cap.fire(c, { x, y, z, yaw, pitch, weapon, settleTics: flags.after ? Number(flags.after) : undefined });
        const mark = await cap.findMark(c);
        if (!mark) { console.log(JSON.stringify({ tag, weapon, marked: false })); break; }
        const cam = await cap.placeCamera(c, mark, yaw, {
          back: flags.back ? Number(flags.back) : undefined,
          up: flags.up ? Number(flags.up) : undefined,
        });
        if (!cam) { console.log(JSON.stringify({ tag, weapon, mark, camera: null })); break; }
        await cap.waitTics(c, 6);
        // shotPair, not a second capture path: it PAUSES the sim between the two halves, which is
        // the only way a fading decal appears in both.
        const shots = await shot.shotPair(c, tag, { engineBin: flags.engine || resolveEngine() });
        console.log(JSON.stringify({ tag, weapon, mark, camera: cam, ...shots }, null, 2));
      } finally { c.close(); }
      break;
    }
    case "renderer-info": {
      if (!flags.port) { console.error("usage: fuactl renderer-info --port P [--token T]"); process.exit(2); }
      const c = new BridgeClient();
      await c.connect(Number(flags.port), { token: flags.token || null });
      await c.waitHello();
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
          case "screenshot": return ui.screenshot(c, flags.engine || resolveEngine(), a[0] || "fuactl_shot").then((s) => ({ path: s.path, bytes: s.base64.length }));
          case "read":       return ui.readMenu(c).then((m) => (flags.full ? m : { lines: m.lines.map((l) => l.text) }));
          case "find":       return ui.findLabel(c, a.join(" "));
          case "warp":       return ui.warp(c, Number(a[0]), Number(a[1]));
          case "damaging":   return ui.damagingSectors(c, flags.limit ? Number(flags.limit) : 64);
          case "exec":       return c.rpc("console.exec", { text: a.join(" ") });
          default: throw new Error(`unknown ui action: ${act} (nav/click/rightclick/drag/type/look/stick/screenshot/exec)`);
        }
      });
      console.log(JSON.stringify(r, null, 2));
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
