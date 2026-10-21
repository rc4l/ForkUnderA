// Run a scenario repeatedly and report the spread, discarding runs that did not actually happen.
//
// [rc4l] The two failures this is built around, both from one session:
//   - a change was called a regression on the strength of one run either side of it; five repeats
//     of the SAME build spanned 174.5-181.3 ms, so both numbers were inside the noise
//   - one run in five measured a level where the trigger had not fired, reporting a p99 five times
//     better than the real one -- an invalid run that flatters the result is worse than a crash
// So: repeat by default, report the band, and throw invalid runs out loudly rather than averaging
// them in quietly.
import { runScenario } from "./scenario.mjs";
import { summarise, compare, pluck } from "./stats.mjs";

export async function runBench(c, scenario, { runs = 5, metric = "total.p99_ms", log = () => {} } = {}) {
  const kept = [], discarded = [];

  for (let i = 0; i < runs; i++) {
    const r = await runScenario(c, scenario);
    const value = pluck(r.report, metric);

    if (r.failures.length) {
      discarded.push({ run: i + 1, why: r.failures, observed: r.observed });
      log(`run ${i + 1}/${runs} DISCARDED: ${r.failures.join("; ")}`);
      continue;
    }
    if (typeof value !== "number" || !Number.isFinite(value)) {
      discarded.push({ run: i + 1, why: [`metric ${metric} missing from the report`], observed: r.observed });
      log(`run ${i + 1}/${runs} DISCARDED: no ${metric}`);
      continue;
    }

    kept.push({ run: i + 1, value, observed: r.observed, report: r.report });
    log(`run ${i + 1}/${runs} ${metric}=${value}`);
  }

  return {
    scenario: scenario.name || "(unnamed)",
    metric,
    runs_requested: runs,
    // Surfaced, never silent: a bench that quietly dropped half its runs is not a result.
    runs_kept: kept.length,
    runs_discarded: discarded.length,
    discarded,
    values: kept.map((k) => k.value),
    summary: summarise(kept.map((k) => k.value)),
    samples: kept,
  };
}

// Two benches, same scenario, different builds. `compare` decides whether the gap survives the
// scatter; this only arranges the runs and keeps the verdict honest about how few there were.
export function benchCompare(baselineBench, candidateBench) {
  const verdict = compare(baselineBench.values, candidateBench.values, { label: baselineBench.metric });

  return {
    metric: baselineBench.metric,
    ...verdict,
    caveat: (baselineBench.runs_kept < 3 || candidateBench.runs_kept < 3)
      ? "fewer than 3 valid runs on one side -- treat any verdict as provisional"
      : undefined,
  };
}
