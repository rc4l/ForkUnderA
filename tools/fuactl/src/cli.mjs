#!/usr/bin/env node
// fuactl -- ForkUnderA companion. One tool, two faces:
//   fuactl <cmd>     humans / CI          fuactl mcp     agents (MCP stdio server)
// Both talk to the engine's native bridge (features/mcp-bridge), which must be built with
// -DFUA_MCP_BRIDGE=ON (ZX_MCP_BRIDGE=1 ./mac_compile.sh) and armed with ZANDRONUM_BRIDGE_PORT.
import { reap, readRegistry } from "./registry.mjs";
import { runDeterminismCheck, runPerfAblation, runNetBandwidth, runGlTimers } from "./session.mjs";
import { launchInstance, stopInstance, resolveEngine } from "./launch.mjs";
import { BridgeClient } from "./client.mjs";
import { sampleProcess } from "./sample.mjs";
import { summarizeGlTimers } from "./proto.mjs";
import * as ui from "./ui.mjs";
import { runBench } from "./bench.mjs";
import { runSkyProbe } from "./skyprobe.mjs";
import { makeUndisturbed, warpTo, findOutdoorSpot } from "./undisturbed.mjs";
import fs from "node:fs";
import path from "node:path";

const num = (v) => (v != null && v !== true ? Number(v) : undefined);

// [rc4l] Resolve --file entries against the CALLER's cwd and fail loudly on a miss. The engine runs
// with its own cwd (the staged dist dir), so a path relative to the repo silently matched nothing:
// the engine started on a bare IWAD, and a screenshot of Doom 2 MAP01 is indistinguishable from a
// mod that loaded and broke. Cost a full diagnostic cycle before the pixels gave it away.
function resolvePwads(spec) {
  if (!spec) return undefined;
  const files = String(spec).split(",").map((f) => path.resolve(f.trim()));
  const missing = files.filter((f) => !fs.existsSync(f));
  if (missing.length) {
    console.error(`--file: not found:\n  ${missing.join("\n  ")}`);
    process.exit(2);
  }
  return files.flatMap((f) => ["-file", f]);
}

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
      flags[key] = val;
    } else rest.push(a);
  }
  return { flags, rest };
}

const USAGE = `fuactl <command>
  ls                                 list registered engine instances
  reap [--kill] [--all]              prune dead; --kill SIGTERMs ORPHANS only (other sessions safe); --all kills every live instance
  launch [--map M] [--seed S] [--port P] [--token T] [--iwad W] [--skill N] [--file a.wad,b.pk3]   launch one supervised bridge instance (stays up until Ctrl-C)
  launch-calm [same flags]           same, but you start as a SPECTATOR with nothing fighting you: sv_fua_friendlymonsters
                                     set from the command line (so monsters facing the player start never get a first look),
                                     plus god+notarget+fly. --no-spectate to start embodied
  sample --pid P | --port P [--seconds N] [--engine]   hottest functions (macOS sample / Linux perf; unavailable on Windows)
  net-bw [--seed S] [--map M] [--spawn CLS] [--count N] [--seconds N]   client/server bandwidth, baseline vs perturbation
  rpc <cmd> [jsonArgs] --port P [--token T]   send one RPC to an instance and print the result
  session [--instances N] [--seed S] [--map M] [--tics T]   run the determinism + desync check
  perf-ab [--seed S] [--map M] [--spawn CLS] [--count N] [--frames F]   deterministic perf ablation (baseline vs perturbation, causal ms delta + sim/render verdict)
  gl-timers --port P [--token T] [--frames N] [--warmup M]   GPU render profiling: per-pass GPU ms (scene/translucent/hud2d) of a running instance
  capture --port P [--frames N] [--warmup M]   frametime distribution (p50/p95/p99/max, sim vs render split)
  ticprof --port P [--tics N]          per-tic sim phase split (P_Ticker / thinkers / effects / specials)
  bench --port P --scenario F.json [--runs N] [--metric total.p99_ms]   repeat a scenario, report median + spread, discard runs whose expectations failed
  renderer-info --port P [--token T]   renderer identity + whether GL timer queries work on this driver
  warp --port P [--x N --y N --z N] [--angle D] [--pitch D] [--outdoors] [--map M]
                                     go there with god + fly + sv_fua_friendlymonsters on, so nothing fights you
                                     (--no-god/--no-friendly/--no-fly to opt out); --outdoors asks the engine for
                                     this level's sky-lit spot; pitch>0 looks down, so --pitch -90 is straight up
  sky <TEXTURE> --port P             swap sky1 on the live level, so the same sky can be compared across maps
  light --port P (--level N | --delta N | --scale F | --restore) [--sector I]   set/offset/scale sector light, or put back what loaded
  skyprobe --port P --maps M1,M2 [--strength N] [--saturation N]   sky tint per map, measured OUTDOORS: tint, light passed, dE76
  ui <action> [args] --port P [--token T]   drive the UI: read (menu as text), find <label>, nav <keys>, click <x> <y>, drag, type <text>, look --yaw D --pitch D, screenshot [name], exec <ccmd>
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
    case "launch":
    case "launch-calm": {
      // [rc4l] Two front doors on purpose. `launch` starts the game as the game: monsters hostile,
      // player mortal, which is what you want when the thing under test IS the game.
      // `launch-calm` starts it as somewhere to stand and look, with nothing fighting the camera.
      //
      // The cvar goes on the COMMAND LINE rather than being set over the bridge afterwards. By the
      // time a bridge connection exists the level has loaded, its monsters have spawned, and the
      // ones placed facing the player start have already seen you. Setting it as a launch parameter
      // means it is live before P_SetupLevel, so PostBeginPlay catches every monster as it is
      // created and none of them ever gets a first look.
      const calm = cmd === "launch-calm";
      const pwads = resolvePwads(flags.file) || [];

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
        // [rc4l] PWADs, comma-separated. Without this the only launchable thing was a bare IWAD, so
        // a mod could be profiled only by hosting a server first and measuring through the netcode.
        extraArgs: calm ? [...pwads, "+sv_fua_friendlymonsters", "1"] : pwads,
      });
      console.log(`launched pid=${inst.pid} port=${inst.port} token=${inst.token}`);

      if (calm) {
        // god/notarget/fly are per-PLAYER state, so unlike the cvar they cannot be set before a
        // player exists. Applied once the bridge answers, which is the earliest they can be.
        //
        // launchInstance returns as soon as the process is spawned, well before the engine has
        // finished starting up and opened its socket, so the first connect is refused rather than
        // slow. Retried rather than waited out with a fixed sleep, which is either too short on a
        // cold start or wasted time on a warm one.
        const c = new BridgeClient();
        for (let attempt = 0; ; attempt++) {
          try {
            await c.connect(inst.port, { token: inst.token, timeoutMs: 5000 });
            break;
          } catch (e) {
            if (attempt >= 30) throw e;
            await new Promise((r) => setTimeout(r, 1000));
          }
        }
        await c.waitHello();
        const on = await makeUndisturbed(c, {
          god: !flags["no-god"],
          notarget: !(flags["no-friendly"] || flags["no-notarget"]),
          fly: !flags["no-fly"],
          // Start as a spectator: no body to shoot at, and it walks through geometry, so a level is
          // somewhere to move around and look rather than something to survive.
          spectate: !flags["no-spectate"],
        });
        c.close();
        console.log(`calm: ${Object.keys(on).filter((k) => on[k]).join(" + ") || "nothing applied"}`);
      }

      console.log(`(rpc it with: fuactl rpc sim.tic --port ${inst.port} --token ${inst.token})`);
      process.on("SIGINT", async () => { await stopInstance(inst); process.exit(0); });
      await new Promise(() => {}); // stay up
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
    case "skyprobe": {
      if (!flags.port || !flags.maps) {
        console.error("usage: fuactl skyprobe --port P --maps MAP01,MAP20 [--token T] " +
          "[--strength N] [--saturation N]");
        process.exit(2);
      }
      // Screenshots land beside the engine, because that is where it writes them.
      const enginePath = flags.engine || resolveEngine();
      await runSkyProbe({
        port: flags.port,
        token: flags.token || null,
        engine: enginePath,
        outDir: path.dirname(enginePath),
        maps: String(flags.maps).split(",").map((m) => m.trim()).filter(Boolean),
        strength: flags.strength != null ? Number(flags.strength) : null,
        saturation: flags.saturation != null ? Number(flags.saturation) : null,
      });
      break;
    }
    case "warp": {
      // fuactl warp --port P [--x N --y N | --outdoors] [--map M]
      //
      // Always turns on god and notarget first. Anywhere worth warping to for a look is somewhere
      // the level would rather you did not stand: measuring or eyeballing a spot while a room full
      // of things shoots at you gets you muzzle flashes and blood in every frame, and eventually a
      // death that moves the camera somewhere else entirely.
      if (!flags.port) {
        console.error("usage: fuactl warp --port P [--x N --y N [--z N]] [--outdoors] [--map M]\n" +
          "                  [--angle DEG] [--pitch DEG] [--no-god] [--no-friendly] [--no-fly]");
        process.exit(2);
      }
      const c = new BridgeClient();
      await c.connect(Number(flags.port), { token: flags.token || null, timeoutMs: 8000 });
      await c.waitHello();

      if (flags.map) {
        await c.rpc("console.exec", { text: `map ${flags.map}` });
        await new Promise((r) => setTimeout(r, 8500));
      }

      // After the map change, not before: cheats reset with the player. All of it is on unless
      // switched off, because the reason to warp somewhere is almost never to fight what lives there.
      // --no-notarget is kept as a spelling of --no-friendly: pacifying the level used to be done
      // with the notarget cheat, and that is still what a caller means when they ask for it.
      const applied = await makeUndisturbed(c, {
        god: !flags["no-god"],
        notarget: !(flags["no-friendly"] || flags["no-notarget"]),
        fly: !flags["no-fly"],
      });
      const on = Object.keys(applied).filter((k) => applied[k]);
      console.log(on.length ? `${on.join(" + ")} on` : "no cheats applied");

      let x = flags.x != null ? Number(flags.x) : null;
      let y = flags.y != null ? Number(flags.y) : null;
      if (flags.outdoors) {
        const spot = await findOutdoorSpot(c);
        if (!spot) {
          console.error("no outdoor spot on this level (nothing sees sky)");
          c.close();
          process.exit(1);
        }
        x = spot.x; y = spot.y;
      }

      if (x == null || y == null) {
        console.log("no destination given, staying put");
      } else {
        const at = await warpTo(c, x, y, {
          z: flags.z != null ? Number(flags.z) : undefined,
          angle: flags.angle != null ? Number(flags.angle) : undefined,
          pitch: flags.pitch != null ? Number(flags.pitch) : undefined,
        });
        // Report where the engine put you, not where you asked to go: z gets clamped into whatever
        // gap the pawn fits in, so the two differ whenever the destination was inside geometry.
        if (at) {
          console.log(`at ${at.x} ${at.y} ${at.z}  angle ${Math.round(at.angle)} pitch ` +
            `${Math.round(at.pitch)}  sector ${at.sector}`);
        }
      }
      c.close();
      break;
    }
    case "sky": {
      // fuactl sky <TEXTURE> --port P
      //
      // Swaps sky1 on the live level. The point is isolating cause: when one map's tint reads strong
      // and another's reads invisible, this puts the same sky on both, so the only thing that changed
      // is the map. changesky already tells features/sky-tint the sky moved, so the table rebuilds.
      const tex = rest[0];
      if (!tex || !flags.port) {
        console.error("usage: fuactl sky <TEXTURE> --port P [--token T]");
        process.exit(2);
      }
      const c = new BridgeClient();
      await c.connect(Number(flags.port), { token: flags.token || null, timeoutMs: 8000 });
      await c.waitHello();
      const said = [];
      const off = c.onEvent((n, d) => { if (n === "out" && d && d.text) said.push(d.text.trim()); });
      await c.rpc("console.exec", { text: `changesky ${tex}` });
      await new Promise((r) => setTimeout(r, 900));
      off();
      // changesky prints only on failure, so silence is success. Reported either way rather than
      // leaving a typo'd texture name to look like a sky that simply has no effect.
      const bad = said.find((l) => /not found/i.test(l));
      console.log(bad ? bad : `sky is now ${tex}`);
      c.close();
      if (bad) process.exit(1);
      break;
    }
    case "light": {
      // fuactl light --level N | --delta N | --scale F | --restore  [--sector I] --port P
      if (!flags.port) {
        console.error("usage: fuactl light --port P (--level N | --delta N | --scale F | --restore)\n" +
          "                   [--sector I]   sector light: set, offset, scale, or put back what loaded");
        process.exit(2);
      }
      const req = {};
      if (flags.sector != null) req.sector = Number(flags.sector);
      if (flags.restore) req.op = "restore";
      else if (flags.level != null) req.level = Number(flags.level);
      else if (flags.delta != null) req.delta = Number(flags.delta);
      else if (flags.scale != null) req.scale = Number(flags.scale);
      else {
        console.error("need one of --level, --delta, --scale, --restore");
        process.exit(2);
      }
      const c = new BridgeClient();
      await c.connect(Number(flags.port), { token: flags.token || null, timeoutMs: 8000 });
      await c.waitHello();
      const r = await c.rpc("world.light", req);
      console.log(`${r.sectors} sectors, light now ${r.min}..${r.max}${r.restored ? " (restored)" : ""}`);
      c.close();
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
