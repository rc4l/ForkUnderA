// Session orchestration: stand up N engine instances, drive them in lockstep, and assert cross-
// instance invariants. This is the "programmable engine" thesis in code -- deterministic sim +
// multi-instance + desync detection, none of which a console-scraping bolt-on could do.
import { launchInstance, stopInstance } from "./launch.mjs";
import { BridgeClient } from "./client.mjs";
import { desyncVerdict } from "./proto.mjs";
import { sampleProcess } from "./sample.mjs";

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function attach(inst, { retries = 60, delayMs = 500 } = {}) {
  // The engine takes a few seconds to boot and bind its bridge port; retry ECONNREFUSED until it's up.
  for (let i = 0; ; i++) {
    const c = new BridgeClient();
    try {
      await c.connect(inst.port, { token: inst.token, timeoutMs: 2000 });
      await c.waitHello();
      return c;
    } catch (e) {
      c.close();
      if (i >= retries || !/ECONNREFUSED|timeout/.test(e.message)) throw e;
      if (inst.proc && inst.proc.exitCode != null) throw new Error("engine exited before bridge came up");
      await sleep(delayMs);
    }
  }
}

async function waitInLevel(c, timeoutMs = 20000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const t = await c.rpc("sim.tic");
    if (t.inlevel) return t;
    await sleep(200);
  }
  throw new Error("instance never reached GS_LEVEL");
}

// Step an instance to (or past) an absolute leveltime target and wait for the refreeze. Returns the
// ACTUAL leveltime it settled at -- which may exceed the target, because the engine refreezes on
// level.time >= target and TryRunTics can run several tics in one frame during catch-up (overshoot).
async function stepTo(c, targetLevelTime) {
  const t = await c.rpc("sim.tic");
  const need = targetLevelTime - t.leveltime;
  if (need <= 0) return t.leveltime;
  const stepped = c.waitEvent("stepped", 20000);
  await c.rpc("sim.step", { tics: need });
  await stepped;
  return (await c.rpc("sim.tic")).leveltime; // read the real settled time, not the requested target
}

// Bring every instance to ONE common leveltime before comparing fingerprints. A single large step
// can overshoot by different amounts per instance, so we level everyone UP to the current max with
// single-tic top-ups and repeat until they agree (you can't un-step). This is what makes the
// determinism comparison exact rather than occasionally off-by-a-tic.
async function alignAll(clients, maxIters = 8) {
  for (let iter = 0; iter < maxIters; iter++) {
    const times = [];
    for (const c of clients) times.push((await c.rpc("sim.tic")).leveltime);
    const max = Math.max(...times);
    if (times.every((t) => t === max)) return max;
    for (let i = 0; i < clients.length; i++) if (times[i] < max) await stepTo(clients[i], max);
  }
  const final = [];
  for (const c of clients) final.push((await c.rpc("sim.tic")).leveltime);
  throw new Error(`instances would not converge to a common leveltime: ${JSON.stringify(final)}`);
}

async function capturePerf(c, frames) {
  const done = c.waitEvent("perf", 40000);
  await c.rpc("perf.capture", { frames });
  return done; // { total: {mean_ms, p99_ms, fps_avg, fps_1pct_low, ...}, sim_mean_ms, render_mean_ms }
}

// Deterministic perf ABLATION: measure a scene, apply a perturbation with everything else held
// constant, measure again, and diff -- so the frametime delta is CAUSAL. Attributes the cost to
// sim (CPU) vs render (GPU-ish) via the coarse split, and correlates with the actor-count jump.
// This is the "what/why is the flamethrower lagging" method in code.
export async function runPerfAblation(opts = {}) {
  const {
    seed = 20260812, map = "MAP01", iwad = "freedoom2.wad", engine,
    frames = 120, spawn = "DoomImp", count = 40, log = () => {},
  } = opts;

  const insts = [await launchInstance({ seed, map, iwad, engine })];
  const clients = [];
  try {
    clients.push(await (async () => {
      const c = new BridgeClient();
      for (let i = 0; ; i++) {
        try { await c.connect(insts[0].port, { token: insts[0].token, timeoutMs: 2000 }); await c.waitHello(); return c; }
        catch (e) { c.close(); if (i >= 60) throw e; await sleep(500); }
      }
    })());
    const c = clients[0];
    await waitInLevel(c);
    await c.rpc("sim.resume"); // frames + effects must advance during capture

    log(`baseline capture (${frames} frames)…`);
    const base = await capturePerf(c, frames);
    const baseCounters = await c.rpc("perf.counters");

    log(`perturbing: summon ${count}x ${spawn}…`);
    await c.rpc("console.exec", { text: "sv_cheats 1" });
    for (let i = 0; i < count; i++) await c.rpc("console.exec", { text: `summon ${spawn}` });
    await sleep(600); // let the spawns settle into the scene

    log(`perturbed capture (${frames} frames) + function sampling…`);
    // Sample the running engine DURING the perturbed window for function-level attribution -- which
    // functions are hot (called a lot) while the load is active. No source instrumentation.
    const [perturbed, sample] = await Promise.all([
      capturePerf(c, frames),
      sampleProcess(insts[0].pid, { seconds: 2, top: 12, engineOnly: true }),
    ]);
    const perturbedCounters = await c.rpc("perf.counters");

    const dTotal = perturbed.total.mean_ms - base.total.mean_ms;
    const dSim = perturbed.sim_mean_ms - base.sim_mean_ms;
    const dRender = perturbed.render_mean_ms - base.render_mean_ms;
    const verdict = dRender >= dSim ? "render/GPU-dominated" : "sim/CPU-dominated";

    return {
      scenario: { seed, map, spawn, count, frames },
      baseline: { fps_avg: base.total.fps_avg, mean_ms: base.total.mean_ms, actors: baseCounters.actors },
      perturbed: { fps_avg: perturbed.total.fps_avg, mean_ms: perturbed.total.mean_ms, fps_1pct_low: perturbed.total.fps_1pct_low, actors: perturbedCounters.actors },
      delta_ms: { total: dTotal, sim: dSim, render: dRender },
      actor_delta: perturbedCounters.actors - baseCounters.actors,
      verdict,
      hot_functions: sample.available ? sample.functions : { unavailable: sample.error },
    };
  } finally {
    for (const c of clients) c.close();
    for (const inst of insts) await stopInstance(inst);
  }
}

// Deterministic RECEIVE-bandwidth ablation over a real server+client session: connect a client to a
// hosted server, measure per-command (per-SVC) receive bytes, perturb the server (spawn moving actors
// -- the same replication traffic a fired projectile/smoke generates), measure again, diff. Answers
// "what am I receiving that costs so much, per command."
export async function runNetBandwidth(opts = {}) {
  const {
    seed = 20260812, map = "MAP01", iwad = "freedoom2.wad", engine,
    gamePort = 10800, seconds = 3, spawn = "DoomImp", count = 60, log = () => {},
  } = opts;

  // Host flags, each learned the hard way:
  //  -port (COMMAND-LINE, not a "+port" cvar which the engine ignores -> it would bind the default
  //   10666 and the client's connect would hang forever).
  //  +sv_cheats 1 must be set AT LAUNCH: a runtime change never reaches an already-connected client,
  //   so the client's `summon` is refused with "sv_cheats must be true". Set here it lands in the
  //   client's initial cvar sync.
  const server = await launchInstance({ seed, map, iwad, engine, extraArgs: ["-host", "-port", String(gamePort), "+sv_broadcast", "0", "+sv_updatemaster", "0", "+sv_cheats", "1"] });
  let client = null;
  const sc = new BridgeClient(), cc = new BridgeClient();
  const connect = async (inst, c) => { for (let i = 0; ; i++) { try { await c.connect(inst.port, { token: inst.token, timeoutMs: 2000 }); await c.waitHello(); return; } catch (e) { c.close(); if (i >= 60) throw e; await sleep(500); } } };
  try {
    await connect(server, sc);
    await waitInLevel(sc); // server hosting the map
    // Launch the client with +connect (NO local map) only after the server is confirmed hosting.
    // If the client started in its own +map it would already be GS_LEVEL, so waitInLevel below would
    // pass instantly -- before the netgame join -- and measure zero receive traffic. This was the bug.
    log(`server hosting; launching client -> 127.0.0.1:${gamePort}…`);
    client = await launchInstance({ seed, iwad, engine, connect: `127.0.0.1:${gamePort}` });
    await connect(client, cc);
    await waitInLevel(cc, 30000); // client reaches GS_LEVEL only once the connection completes
    // A freshly-connected client is a SPECTATOR -- it receives only keepalives (~8 B/s). It must
    // `join` to become an active player and receive the real replication stream (~1 kB/s).
    await cc.rpc("console.exec", { text: "join" });
    await sleep(1000);
    log("client joined; measuring baseline receive bandwidth…");

    const capture = async () => {
      await cc.rpc("net.bandwidth", { op: "reset" });
      await sleep(seconds * 1000);
      return cc.rpc("net.bandwidth", { top: 10 });
    };
    const base = await capture();

    // Perturb by spawning actors the CLIENT will receive. The -host server is dedicated (no console
    // player pawn), so `summon` on the server is a no-op -- it spawns at the caller's pawn. Issue it
    // from the CLIENT instead: with cheats synced at launch the request reaches the server, which
    // spawns near the client's pawn and replicates the moving actors back => real receive traffic
    // (it shows up as a distinct actor-update SVC, separate from the steady position stream).
    log(`perturbing: client summons ${count}x ${spawn} (server-authoritative -> replicated back)…`);
    for (let i = 0; i < count; i++) await cc.rpc("console.exec", { text: `summon ${spawn}` });
    await sleep(800);
    const perturbed = await capture();

    return {
      scenario: { seed, map, spawn, count, seconds },
      baseline: { bytes_per_s: base.bytes_per_s, total: base.total_bytes, top: base.top },
      perturbed: { bytes_per_s: perturbed.bytes_per_s, total: perturbed.total_bytes, top: perturbed.top },
      delta_bytes_per_s: perturbed.bytes_per_s - base.bytes_per_s,
    };
  } finally {
    sc.close(); cc.close();
    if (client) await stopInstance(client);
    await stopInstance(server);
  }
}

// The determinism + desync check. Returns a structured report; throws only on infra failure.
export async function runDeterminismCheck(opts = {}) {
  const {
    instances = 2, seed = 20260812, map = "MAP01", iwad = "freedoom2.wad",
    tics = 70, engine, log = () => {},
  } = opts;

  const report = { seed, map, instances, steps: [], pass: false };
  const insts = [];
  const clients = [];
  try {
    log(`launching ${instances} instances (seed=${seed}, map=${map})…`);
    for (let i = 0; i < instances; i++) {
      insts.push(await launchInstance({ seed, map, iwad, engine }));
    }
    for (const inst of insts) clients.push(await attach(inst));
    for (const c of clients) await waitInLevel(c);
    log("all instances in level.");

    // Pause everyone, then bring them to a common leveltime so we compare like-for-like.
    for (const c of clients) await c.rpc("sim.pause");
    let maxT = 0;
    for (const c of clients) maxT = Math.max(maxT, (await c.rpc("sim.tic")).leveltime);
    const target = maxT + tics;
    log(`stepping all instances to leveltime ${target}…`);
    for (const c of clients) await stepTo(c, target);
    // stepTo can overshoot by different amounts per instance -- align to an exact common leveltime
    // before hashing, or a comparison "failure" is really just an off-by-a-tic in the harness.
    const alignedT = await alignAll(clients);

    // Invariant 1: identical seed+map+tics => identical fingerprint (determinism).
    const h1 = [];
    for (const c of clients) h1.push((await c.rpc("sim.hash")).hash);
    const v1 = desyncVerdict(h1);
    report.steps.push({ name: "determinism", leveltime: alignedT, hashes: h1, agree: v1.agree });
    log(`determinism @${alignedT}: hashes=${JSON.stringify(h1)} agree=${v1.agree}`);

    // Invariant 2: perturb ONE instance's RNG state (a real desync cause) => fingerprints diverge,
    // and we detect it. Then step all one more tic so leveltime still matches across instances.
    await clients[0].rpc("sim.seed", { op: "set", value: seed ^ 0x5bd1e995 });
    const target2 = alignedT + 1;
    for (const c of clients) await stepTo(c, target2);
    const alignedT2 = await alignAll(clients); // leveltime is RNG-independent, so this still aligns
    const h2 = [];
    for (const c of clients) h2.push((await c.rpc("sim.hash")).hash);
    const v2 = desyncVerdict(h2);
    report.steps.push({ name: "desync-detect", leveltime: alignedT2, hashes: h2, diverged: !v2.agree });
    log(`desync @${alignedT2}: hashes=${JSON.stringify(h2)} diverged=${!v2.agree}`);

    report.pass = v1.agree && !v2.agree;
    return report;
  } finally {
    for (const c of clients) c.close();
    for (const inst of insts) await stopInstance(inst);
  }
}
