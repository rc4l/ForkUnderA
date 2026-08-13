// Session orchestration: stand up N engine instances, drive them in lockstep, and assert cross-
// instance invariants. This is the "programmable engine" thesis in code -- deterministic sim +
// multi-instance + desync detection, none of which a console-scraping bolt-on could do.
import { launchInstance, stopInstance } from "./launch.mjs";
import { BridgeClient } from "./client.mjs";
import { desyncVerdict } from "./proto.mjs";

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

// Step an instance to an absolute leveltime target and wait for the refreeze.
async function stepTo(c, targetLevelTime) {
  const t = await c.rpc("sim.tic");
  const need = targetLevelTime - t.leveltime;
  if (need <= 0) return t.leveltime;
  const stepped = c.waitEvent("stepped", 20000);
  await c.rpc("sim.step", { tics: need });
  await stepped;
  return targetLevelTime;
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

    log(`perturbed capture (${frames} frames)…`);
    const perturbed = await capturePerf(c, frames);
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
    };
  } finally {
    for (const c of clients) c.close();
    for (const inst of insts) await stopInstance(inst);
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

    // Invariant 1: identical seed+map+tics => identical fingerprint (determinism).
    const h1 = [];
    for (const c of clients) h1.push((await c.rpc("sim.hash")).hash);
    const v1 = desyncVerdict(h1);
    report.steps.push({ name: "determinism", leveltime: target, hashes: h1, agree: v1.agree });
    log(`determinism @${target}: hashes=${JSON.stringify(h1)} agree=${v1.agree}`);

    // Invariant 2: perturb ONE instance's RNG state (a real desync cause) => fingerprints diverge,
    // and we detect it. Then step all one more tic so leveltime still matches across instances.
    await clients[0].rpc("sim.seed", { op: "set", value: seed ^ 0x5bd1e995 });
    const target2 = target + 1;
    for (const c of clients) await stepTo(c, target2);
    const h2 = [];
    for (const c of clients) h2.push((await c.rpc("sim.hash")).hash);
    const v2 = desyncVerdict(h2);
    report.steps.push({ name: "desync-detect", leveltime: target2, hashes: h2, diverged: !v2.agree });
    log(`desync @${target2}: hashes=${JSON.stringify(h2)} diverged=${!v2.agree}`);

    report.pass = v1.agree && !v2.agree;
    return report;
  } finally {
    for (const c of clients) c.close();
    for (const inst of insts) await stopInstance(inst);
  }
}
