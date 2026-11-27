// [Doruk-Aksoy] The tail-loss scenario's verdicts: an arrival timed by polling, a change that never came failing, and a drop never spent proving nothing.
import { test } from "node:test";
import assert from "node:assert/strict";
import { msUntil, arrivalVerdict, buildTailLossWad, CVAR, ARRIVE_MS, CONFIGS } from "../scenarios/nettailloss.mjs";

test("the time until a read answers is polled, and null once the time allowed has gone", async () => {
  let clock = 0, reads = 0;
  const deps = { sleep: async (ms) => { clock += ms; }, now: () => clock };
  assert.equal(await msUntil(async () => (++reads >= 4 ? "true" : "false"), "true", 3000, deps), 300);
  clock = 0;
  assert.equal(await msUntil(async () => "false", "true", 1000, deps), null);
  assert.equal(clock, 1000);
});

test("a change in time passes, one that never came fails, and an unspent drop fails as proving nothing", () => {
  assert.deepEqual(arrivalVerdict("MAP01 lan true player 0", { took: 640, want: "true" }), { line: `PASS MAP01 lan true player 0: ${CVAR} reached true after 640 ms`, failed: 0 });
  assert.deepEqual(arrivalVerdict("x", { took: null, want: "false" }), { line: `FAIL x: ${CVAR} never reached false within ${ARRIVE_MS / 1000} s`, failed: 1 });
  assert.match(arrivalVerdict("x", { took: 10, dropLeft: 1, want: "true" }).line, /never threw a reliable packet away/);
  // [Doruk-Aksoy] Seen on the unfixed build: the player never got the first change, so the second, back to its old value, read as arrived.
  assert.deepEqual(arrivalVerdict("x", { took: 0, want: "false", before: "false" }), { line: "FAIL x: the client held false before the change, so its arrival proves nothing", failed: 1 });
});

test("a quiet room with the two starts and no monster, hosted in snapshot mode on loopback and at 100 ms", () => {
  const text = buildTailLossWad().toString("latin1");
  assert.equal(text.split("type = 1;").length - 1, 1);
  assert.equal(text.split("type = 2;").length - 1, 1);
  assert.deepEqual(CONFIGS.map((c) => c.name), ["lan", "ping100"]);
  for (const c of CONFIGS) assert.deepEqual([c.players, c.observers], [1, 1]);
  for (const c of CONFIGS) assert.deepEqual(c.serverCvars, { fua_snap: 1, fua_snap_send: 1, fua_snap_nolegacy: 2 }, "the snapshot netcode, whose datagrams name the newest reliable sequence");
});
