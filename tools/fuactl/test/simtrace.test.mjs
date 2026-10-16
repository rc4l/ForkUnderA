import { test } from "node:test";
import assert from "node:assert/strict";
import { describeSimTraceDiff, diffSimTraces, parseSimTrace } from "../src/simtrace.mjs";

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
