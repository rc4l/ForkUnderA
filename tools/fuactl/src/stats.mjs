// Turning several runs into a claim you can defend.
//
// [rc4l] This exists because a single run was reported as a regression. Two numbers either side of
// a change look like a delta and are not one: five repeats of the identical build put p99 between
// 174.5 and 181.3 ms, so the "171 -> 187" that prompted a revert was two draws from one
// distribution. Anything comparing builds has to answer "is this bigger than the spread" first,
// and that question needs a spread to exist.

// Percentile by nearest rank on a sorted copy. Small samples here (5-20 runs), so exactness of the
// interpolation matters less than never being surprised by it.
export function percentile(values, p) {
  const v = [...values].sort((a, b) => a - b);
  if (!v.length) return null;
  if (v.length === 1) return v[0];

  const rank = (p / 100) * (v.length - 1);
  const lo = Math.floor(rank), hi = Math.ceil(rank);
  return lo === hi ? v[lo] : v[lo] + (v[hi] - v[lo]) * (rank - lo);
}

export function summarise(values) {
  const v = values.filter((x) => typeof x === "number" && Number.isFinite(x));
  if (!v.length) return null;

  const sorted = [...v].sort((a, b) => a - b);
  const mean = v.reduce((s, x) => s + x, 0) / v.length;
  // Sample standard deviation (n-1): with 5 runs the population form understates the spread, which
  // is the exact direction that makes a non-result look real.
  const variance = v.length > 1 ? v.reduce((s, x) => s + (x - mean) ** 2, 0) / (v.length - 1) : 0;

  return {
    n: v.length,
    min: +sorted[0].toFixed(3),
    max: +sorted[sorted.length - 1].toFixed(3),
    mean: +mean.toFixed(3),
    median: +percentile(v, 50).toFixed(3),
    stddev: +Math.sqrt(variance).toFixed(3),
  };
}

// Is a difference between two sets of runs bigger than the runs' own scatter?
//
// Deliberately NOT a p-value: with five runs on a games machine that would be false precision. The
// rule is blunt and honest -- if the medians differ by less than the combined spread, the honest
// answer is "no measurable difference", which is what should have been said the first time.
export function compare(baseline, candidate, { label = "value" } = {}) {
  const a = summarise(baseline), b = summarise(candidate);
  if (!a || !b) return { label, verdict: "insufficient data", baseline: a, candidate: b };

  const delta = b.median - a.median;
  const noise = a.stddev + b.stddev;
  const pct = a.median !== 0 ? (delta / a.median) * 100 : 0;

  let verdict;
  if (Math.abs(delta) <= noise) verdict = "no measurable difference";
  else if (delta < 0) verdict = "candidate faster";
  else verdict = "candidate slower";

  return {
    label,
    verdict,
    baseline: a,
    candidate: b,
    delta_median: +delta.toFixed(3),
    delta_pct: +pct.toFixed(2),
    noise_band: +noise.toFixed(3),
    // Spelled out so a reader does not have to reconstruct why a real-looking delta was dismissed.
    reason: Math.abs(delta) <= noise
      ? `|${delta.toFixed(1)}| is within the combined spread of ${noise.toFixed(1)}`
      : `|${delta.toFixed(1)}| exceeds the combined spread of ${noise.toFixed(1)}`,
  };
}

// Pull one number out of a report so runs can be compared on it. Dotted path, because the reports
// are nested differently per measurement kind.
export function pluck(report, path) {
  return path.split(".").reduce((o, k) => (o == null ? undefined : o[k]), report);
}
