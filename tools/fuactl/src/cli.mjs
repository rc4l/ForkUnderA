#!/usr/bin/env node
// fuactl -- ForkUnderA companion. One tool, two faces:
//   fuactl <cmd>     humans / CI          fuactl mcp     agents (MCP stdio server)
// Both talk to the engine's native bridge (features/mcp-bridge), which must be built with
// -DFUA_MCP_BRIDGE=ON (ZX_MCP_BRIDGE=1 ./mac_compile.sh) and armed with ZANDRONUM_BRIDGE_PORT.
import { reap, readRegistry } from "./registry.mjs";
import { runDeterminismCheck, runPerfAblation, runNetBandwidth } from "./session.mjs";
import { launchInstance, stopInstance, resolveEngine } from "./launch.mjs";
import { BridgeClient } from "./client.mjs";
import { sampleProcess } from "./sample.mjs";
import * as ui from "./ui.mjs";

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
  launch [--map M] [--seed S]        launch one supervised bridge instance (stays up until Ctrl-C)
  rpc <cmd> [jsonArgs] --port P [--token T]   send one RPC to an instance and print the result
  session [--instances N] [--seed S] [--map M] [--tics T]   run the determinism + desync check
  perf-ab [--seed S] [--map M] [--spawn CLS] [--count N] [--frames F]   deterministic perf ablation (baseline vs perturbation, causal ms delta + sim/render verdict)
  ui <action> [args] --port P [--token T]   drive the UI: nav <keys>, click <x> <y>, drag, type <text>, look --yaw D --pitch D, screenshot [name], exec <ccmd>
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
      const inst = await launchInstance({ map: flags.map, seed: flags.seed != null ? Number(flags.seed) : undefined });
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
      const r = await sampleProcess(pid, { seconds: flags.seconds ? Number(flags.seconds) : 2, engineOnly: !!flags.engine });
      console.log(JSON.stringify(r, null, 2));
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
          case "click":      return ui.click(c, Number(a[0]), Number(a[1]), { button: flags.button || "left", double: !!flags.double }).then(() => ({ clicked: [Number(a[0]), Number(a[1])], button: flags.button || "left" }));
          case "rightclick": return ui.rightClick(c, Number(a[0]), Number(a[1])).then(() => ({ rightClicked: [Number(a[0]), Number(a[1])] }));
          case "drag":       return ui.drag(c, Number(a[0]), Number(a[1]), Number(a[2]), Number(a[3])).then(() => ({ dragged: a.map(Number) }));
          case "type":       return ui.typeText(c, a.join(" ")).then(() => ({ typed: a.join(" ") }));
          case "look":       return ui.look(c, { yaw: flags.yaw ? Number(flags.yaw) : 0, pitch: flags.pitch ? Number(flags.pitch) : 0 });
          case "stick":      return c.rpc("input.axis", flags.clear ? { clear: true } : { yaw: num(flags.yaw), pitch: num(flags.pitch), forward: num(flags.forward), side: num(flags.side) });
          case "screenshot": return ui.screenshot(c, flags.engine || resolveEngine(), a[0] || "fuactl_shot").then((s) => ({ path: s.path, bytes: s.base64.length }));
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
