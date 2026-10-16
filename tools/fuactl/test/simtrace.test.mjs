import { test } from "node:test";
import assert from "node:assert/strict";
import { describeSimTraceDiff, diffSimTraces, formatTic, parseSimTrace } from "../src/simtrace.mjs";

const A = "10 S DoomImp 1 2 3 -\n10 H 111\n11 D DoomImp 1 2 3 10 Rocket\n11 H 222\n12 H 333\n";

test("a trace is its tics' fingerprints and its events in order", () => {
  const t = parseSimTrace(A);
  assert.deepEqual([...t.hashes.entries()], [[10, "111"], [11, "222"], [12, "333"]]);
  assert.deepEqual(t.events, ["10 S DoomImp 1 2 3 -", "11 D DoomImp 1 2 3 10 Rocket"]);
});

test("two runs that agree say so", () => {
  const d = diffSimTraces(parseSimTrace(A), parseSimTrace(A));
  assert.equal(d.same, true);
  assert.deepEqual(describeSimTraceDiff(d), ["the traces agree: 3 tics hashed alike, 2 events in the same order"]);
});

test("the first tic hashed differently and the first event that moved are named", () => {
  const b = A.replace("12 H 333", "12 H 999").replace("11 D DoomImp 1 2 3 10 Rocket", "12 D DoomImp 1 2 3 10 Rocket");
  const d = diffSimTraces(parseSimTrace(A), parseSimTrace(b));
  assert.equal(d.same, false);
  assert.deepEqual(d.firstTic, { tic: 12, a: "333", b: "999" });
  assert.deepEqual(d.firstEvent, { index: 1, a: "11 D DoomImp 1 2 3 10 Rocket", b: "12 D DoomImp 1 2 3 10 Rocket" });
  assert.deepEqual(describeSimTraceDiff(d), [
    "the sim parts at tic 12: fingerprint 333 against 999",
    "the first event that differs is #1: 11 D DoomImp 1 2 3 10 Rocket against 12 D DoomImp 1 2 3 10 Rocket"]);
});

test("a trace with an event the other lacks, and only tics both hashed, compare", () => {
  const d = diffSimTraces(parseSimTrace("5 H 1\n6 H 2\n5 K Imp 0 0 0 -\n"), parseSimTrace("6 H 2\n7 H 9\n"));
  assert.equal(d.tics, 1);
  assert.equal(d.firstTic, null);
  assert.deepEqual(d.firstEvent, { index: 0, a: "5 K Imp 0 0 0 -", b: null });
  assert.deepEqual(describeSimTraceDiff(d), ["the first event that differs is #0: 5 K Imp 0 0 0 - against (none)"]);
});

test("each fingerprint kind both traces carry is compared and named where it parts", () => {
  const a = "10 H 1\n10 R 5\n10 A 7\n11 H 2\n11 R 6\n11 A 8\n";
  const t = parseSimTrace(a);
  assert.deepEqual([...t.kinds.keys()], ["H", "R", "A"]);
  assert.deepEqual(describeSimTraceDiff(diffSimTraces(t, parseSimTrace(a))), ["the traces agree: 2 tics hashed alike (with random streams, ACS variables), 0 events in the same order"]);
  const d = diffSimTraces(t, parseSimTrace(a.replace("11 R 6", "11 R 9")));
  assert.equal(d.same, false);
  assert.equal(d.firstTic, null);
  assert.deepEqual(describeSimTraceDiff(d), ["the random streams part at tic 11: 6 against 9"]);
});

test("a kind only one trace carries is not compared, so an older baseline still holds", () => {
  const d = diffSimTraces(parseSimTrace("10 H 1\n"), parseSimTrace("10 H 1\n10 P 4\n"));
  assert.equal(d.same, true);
  assert.deepEqual(d.compared, ["H"]);
});

test("an unknown fingerprint letter is an event, not a kind", () => {
  assert.deepEqual(parseSimTrace("10 Z 5\n").events, ["10 Z 5"]);
});

test("a level started again numbers its tics apart, so it compares with its own tics", () => {
  const a = "5 H 1\n6 H 2\n1 H 3\n1 R 9\n2 H 4\n";
  const t = parseSimTrace(a);
  assert.deepEqual([...t.hashes.keys()], [5, 6, 10000001, 10000002]);
  const d = diffSimTraces(t, parseSimTrace(a.replace("2 H 4", "2 H 8")));
  assert.deepEqual(describeSimTraceDiff(d), ["the sim parts at tic 2 of level 2: fingerprint 4 against 8"]);
  assert.equal(formatTic(7), "7");
});

test("a frame-dependent kind that parts is told but does not fail the comparison", () => {
  const a = "10 H 1\n10 L 5\n10 R 3\n";
  const d = diffSimTraces(parseSimTrace(a), parseSimTrace(a.replace("10 L 5", "10 L 6")));
  assert.equal(d.same, true);
  assert.deepEqual(describeSimTraceDiff(d), [
    "the traces agree: 1 tics hashed alike (with random streams), 0 events in the same order",
    "the seen bits part at tic 10, which the frames between tics set, so it is not judged"]);
});
