import { test } from "node:test";
import assert from "node:assert/strict";
import { percentile, summarise, compare, pluck } from "../src/stats.mjs";
import { checkExpectations } from "../src/scenario.mjs";

test("summarise reports the band, not just an average", () => {
  const s = summarise([174.5, 181.3, 177.3, 174.5, 179.2]);

  assert.equal(s.n, 5);
  assert.equal(s.min, 174.5);
  assert.equal(s.max, 181.3);
  assert.equal(s.median, 177.3);
  assert.ok(s.stddev > 0, "a spread of nearly 7ms must not report as zero");
});

test("summarise ignores non-numbers rather than poisoning the mean with NaN", () => {
  const s = summarise([10, null, 20, undefined, NaN, 30]);

  assert.equal(s.n, 3);
  assert.equal(s.mean, 20);
});

test("summarise of nothing is null, not a zero that reads like a measurement", () => {
  assert.equal(summarise([]), null);
  assert.equal(summarise([null, NaN]), null);
});

test("percentile handles the one-value and exact-rank cases", () => {
  assert.equal(percentile([42], 99), 42);
  assert.equal(percentile([1, 2, 3], 50), 2);
  assert.equal(percentile([], 50), null);
});

// [rc4l] THE regression this whole module exists for. 171 and 187 were reported as a before/after
// and prompted a revert; five repeats of the unchanged build spanned 174.5-181.3, so both numbers
// were draws from one distribution. A comparison must say so instead of naming a winner.
test("compare refuses to call a difference that is inside the scatter", () => {
  const before = [174.5, 181.3, 177.3, 174.5, 179.2];
  const after = [176.1, 180.2, 175.0, 178.8, 177.9];

  const r = compare(before, after, { label: "p99" });

  assert.equal(r.verdict, "no measurable difference");
  assert.match(r.reason, /within the combined spread/);
});

test("compare does name a winner when the gap clears the scatter", () => {
  const before = [100, 101, 99, 100, 100];
  const after = [60, 61, 59, 60, 60];

  const r = compare(before, after);

  assert.equal(r.verdict, "candidate faster");
  assert.equal(r.delta_median, -40);
  assert.match(r.reason, /exceeds the combined spread/);
  assert.ok(r.delta_pct < -30);
});

test("compare reports a slower candidate too, not just wins", () => {
  assert.equal(compare([10, 10, 10], [50, 50, 50]).verdict, "candidate slower");
});

test("compare says so when there is nothing to compare", () => {
  assert.equal(compare([], [1, 2, 3]).verdict, "insufficient data");
});

test("pluck reaches into a nested report and returns undefined rather than throwing", () => {
  const report = { total: { p99_ms: 171.3 }, sim_mean_ms: 5.4 };

  assert.equal(pluck(report, "total.p99_ms"), 171.3);
  assert.equal(pluck(report, "sim_mean_ms"), 5.4);
  assert.equal(pluck(report, "total.nope"), undefined);
  assert.equal(pluck(report, "missing.deep.path"), undefined);
});

// [rc4l] The other failure: one run in five measured a level where the trigger never fired and
// reported a p99 five times better than the real one. Unchecked, that reads as an optimisation.
test("checkExpectations catches a run where the scenario did not happen", () => {
  const expect = { actorsSpawned: { min: 1000 } };

  assert.deepEqual(checkExpectations(expect, { actorsSpawned: 6572 }), []);
  assert.deepEqual(checkExpectations(expect, { actorsSpawned: -38 }),
    ["actorsSpawned=-38 below min 1000"]);
});

test("checkExpectations enforces an upper bound and flags what it never saw", () => {
  assert.deepEqual(checkExpectations({ leveltime: { max: 100 } }, { leveltime: 500 }),
    ["leveltime=500 above max 100"]);
  assert.deepEqual(checkExpectations({ actorsSpawned: { min: 1 } }, {}),
    ["actorsSpawned was not observed"]);
  assert.deepEqual(checkExpectations({}, { anything: 1 }), []);
});
