import { test } from "node:test";
import assert from "node:assert/strict";
import {
  drainLines, frameRequest, classify, desyncVerdict, partitionRegistry, candidatePort,
} from "../src/proto.mjs";

test("drainLines splits complete NDJSON and keeps the partial remainder", () => {
  const { messages, rest } = drainLines('{"a":1}\n{"b":2}\n{"c":');
  assert.deepEqual(messages, [{ a: 1 }, { b: 2 }]);
  assert.equal(rest, '{"c":');
});

test("drainLines ignores malformed and blank lines", () => {
  const { messages } = drainLines("\nnot json\n{\"ok\":true}\n");
  assert.deepEqual(messages, [{ ok: true }]);
});

test("frameRequest emits id+cmd, and args only when present", () => {
  assert.equal(frameRequest(1, "ping"), '{"id":1,"cmd":"ping"}\n');
  assert.equal(frameRequest(2, "sim.step", { tics: 5 }), '{"id":2,"cmd":"sim.step","args":{"tics":5}}\n');
  assert.equal(frameRequest(3, "x", {}), '{"id":3,"cmd":"x"}\n'); // empty args omitted
});

test("classify distinguishes hello / event / response", () => {
  assert.equal(classify({ t: "hello" }), "hello");
  assert.equal(classify({ t: "event", event: "stepped" }), "event");
  assert.equal(classify({ id: 7, ok: true }), "response");
  assert.equal(classify({ foo: 1 }), "other");
});

test("desyncVerdict: identical hashes agree, a divergent one is caught", () => {
  assert.deepEqual(desyncVerdict(["9", "9", "9"]), { agree: true, distinct: ["9"] });
  const v = desyncVerdict(["9", "9", "42"]);
  assert.equal(v.agree, false);
  assert.deepEqual(v.distinct.sort(), ["42", "9"]);
});

test("partitionRegistry splits live from stale by injected liveness", () => {
  const entries = [{ pid: 1 }, { pid: 2 }, { pid: 3 }, { bad: true }];
  const alive = new Set([1, 3]);
  const { live, stale } = partitionRegistry(entries, (p) => alive.has(p));
  assert.deepEqual(live.map((e) => e.pid), [1, 3]);
  assert.deepEqual(stale.map((e) => e.pid ?? "?"), [2, "?"]); // dead pid + malformed entry are stale
});

test("candidatePort stays in range and varies by offset", () => {
  assert.equal(candidatePort(20000, 0), 20000);
  assert.equal(candidatePort(20000, 1), 20001);
  assert.ok(candidatePort(39999, 5) >= 20000 && candidatePort(39999, 5) < 40000); // wraps
});
