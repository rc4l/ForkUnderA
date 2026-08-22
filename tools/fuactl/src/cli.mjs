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
import fs from "node:fs";

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
      flags[key] = val;
    } else rest.push(a);
  }
  return { flags, rest };
}

const USAGE = `fuactl <command>
  ls                                 list registered engine instances
  reap [--kill] [--all]              prune dead; --kill SIGTERMs ORPHANS only (other sessions safe); --all kills every live instance
  launch [--map M] [--seed S] [--port P] [--token T] [--iwad W] [--skill N] [--file a.wad,b.pk3] [--arg "-host,+sv_hostname,X"]   launch one supervised bridge instance (stays up until Ctrl-C)
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
  ui <action> [args] --port P [--token T]   drive the UI: read (menu as text), find <label>, nav <keys>, click <x> <y>, drag, type <text>, look --yaw D --pitch D, screenshot [name], exec <ccmd>
  browser --port P [--token T] [--wait S] [--expect-lan] [--expect-country XXX]   refresh the server browser and report what it sees (LAN vs registry, country)
  hostdiag --port P [--token T] [--wait S] [--expect-listed]           ask the registry whether THIS server is reachable from outside (per family)
  continue --port P [--token T] [--expect shown|hidden]   what the Continue button is offering, and whether it is on the bar
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
        // [rc4l] PWADs, comma-separated. Without this the only launchable thing was a bare IWAD, so
        // a mod could be profiled only by hosting a server first and measuring through the netcode.
        // [rc4l] --arg passes raw engine arguments through (repeat with commas), so a harness can
        // start a host, a dedicated server or any other mode without inventing a flag per mode.
        extraArgs: [
          ...(flags.file ? String(flags.file).split(",").flatMap((f) => ["-file", f.trim()]) : []),
          ...(flags.arg ? String(flags.arg).split(",").map((a) => a.trim()) : []),
        ],
      });
      console.log(`launched pid=${inst.pid} port=${inst.port} token=${inst.token}`);
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
