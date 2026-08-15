// A scenario: a repeatable thing to measure, written as data instead of a bespoke script.
//
// [rc4l] Every measurement in the session that produced this file was a hand-written one-off --
// six of them, all the same shape: put the level in a known state, arm a measurement, do the thing
// that hurts, collect. Writing that shape six times is how you end up comparing two runs that were
// not actually the same run.
//
// Two parts matter beyond convenience:
//   `expect`   -- a scenario that did not happen must not report numbers. One run in five of a kill
//                 storm silently measured an empty level and produced a p99 five times better than
//                 the real one; averaged in, that reads as an optimisation.
//   `trigger`  -- fired AFTER the measurement is armed, so the spike lands inside the window rather
//                 than before it.

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Steps are deliberately dumb: {exec} runs a console command, {wait} sleeps. Anything cleverer
// belongs in the caller, not in a scenario file.
async function runSteps(c, steps = []) {
  for (const s of steps) {
    if (s.exec != null) await c.rpc("console.exec", { text: s.exec });
    if (s.wait != null) await sleep(s.wait);
  }
}

// Arm a measurement and return a promise for its async report. Each of these RPCs replies
// immediately and delivers the actual numbers as an event later, which is why `fuactl rpc` alone
// can never see them.
function arm(c, measure) {
  const kind = measure.kind || "capture";

  if (kind === "capture") {
    const done = c.waitEvent("perf", measure.timeoutMs || 120000);
    return c.rpc("perf.capture", { frames: measure.frames || 300, warmup: measure.warmup || 0 }).then(() => done);
  }
  if (kind === "ticprof") {
    const done = c.waitEvent("ticprof", measure.timeoutMs || 120000);
    return c.rpc("perf.ticprof", { tics: measure.tics || 80 }).then(() => done);
  }
  if (kind === "sample") {
    const done = c.waitEvent("sample", ((measure.seconds || 3) + 30) * 1000);
    return c.rpc("perf.sample", {
      seconds: measure.seconds || 3,
      hz: measure.hz || 1000,
      top: measure.top || 20,
      engine: measure.engine ? 1 : 0,
      tic_min: measure.ticMin != null ? measure.ticMin : 0,
      tic_max: measure.ticMax != null ? measure.ticMax : 0,
    }).then(() => done);
  }

  throw new Error(`unknown measure kind: ${kind} (capture/ticprof/sample)`);
}

// Did the scenario actually do what it claims? Returns a list of reasons it did not, empty if fine.
export function checkExpectations(expect = {}, observed = {}) {
  const bad = [];

  for (const [key, rule] of Object.entries(expect)) {
    const value = observed[key];
    if (value == null) { bad.push(`${key} was not observed`); continue; }
    if (rule.min != null && value < rule.min) bad.push(`${key}=${value} below min ${rule.min}`);
    if (rule.max != null && value > rule.max) bad.push(`${key}=${value} above max ${rule.max}`);
  }

  return bad;
}

// Run one iteration against an already-connected instance.
export async function runScenario(c, scenario) {
  const { reset, setup = [], measure = {}, trigger, settle = 0, teardown = [] } = scenario;

  if (reset) {
    await c.rpc("console.exec", { text: `map ${reset}` });
    await sleep(scenario.resetWait || 6000);
  }
  await runSteps(c, setup);

  const before = await c.rpc("perf.counters");
  const report = arm(c, measure);
  if (trigger) await runSteps(c, Array.isArray(trigger) ? trigger : [trigger]);

  const result = await report;
  if (settle) await sleep(settle);
  const after = await c.rpc("perf.counters");
  await runSteps(c, teardown);

  // What `expect` can talk about. Kept small on purpose: these are the facts that say the scenario
  // ran, not the facts being measured.
  const observed = {
    actorsBefore: before.actors,
    actorsAfter: after.actors,
    actorsSpawned: after.actors - before.actors,
    leveltime: after.leveltime,
  };

  return { observed, report: result, failures: checkExpectations(scenario.expect, observed) };
}
