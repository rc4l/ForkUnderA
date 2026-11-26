import { test } from "node:test";
import assert from "node:assert/strict";
import { WANT, judgeQueries } from "../scenarios/acsqueries.mjs";
import { STEPS, LATE, AFTER, STARTS, wantFor, judgeMachine, buildNetQueriesPk3 } from "../scenarios/netacsqueries.mjs";

// [Doruk-Aksoy] A log line of `values` after `tag`, ids standing in for null.
const line = (tag, values) => `${tag} ${values.map((v) => (v === null ? 7 : v === undefined ? 0 : v)).join(" ")}`;
const logsFor = (role) => Object.fromEntries(Object.entries(wantFor(role)).map(([t, v]) => [t, line(t, v)]));

test("the imp dies on the server alone, the queries run before the late joiner, and the class queries that remove a barrel last", () => {
  assert.deepEqual(STEPS.map((s) => [s.script, s.clients !== false, s.server !== false]), [[9, false, true], [10, true, false], [1, true, true], [11, true, true]]);
  assert.deepEqual(LATE.map((s) => s.script), [10, 1, 11]);
  assert.deepEqual(AFTER.map((s) => s.script), [3, 4, 5, 2]);
  assert.ok(STARTS.every(([, y]) => y < 500), "south of every monster");
});

test("FQ_PLAYER counts the game's players and q2 finds the activator among them, the first found unjudged", () => {
  assert.deepEqual(["server", "player", "latejoin"].map((r) => [wantFor(r).q1[13], wantFor(r).q2]), [[2, [2, 1]], [2, [2, 1]], [3, [3, 1]]]);
  assert.equal(wantFor("player").q1[14], undefined);
  assert.equal(WANT.q1[14], 1, "single player still judges it");
  assert.deepEqual(judgeQueries({ q1: line("q1", [1, 0]) }, { q1: [undefined, 0] }), []);
});

test("each machine passes on its own answers and fails on a wrong one", () => {
  const logs = { ...logsFor("observer"), l1: `${line("l1", WANT.l1)}\nfua_QueryCreate: a script already holds 16 queries` };
  assert.deepEqual(judgeMachine(logs, { role: "observer" }), []);
  assert.match(judgeMachine({ ...logs, q2: "q2 2 0" }, { role: "observer" }).join(), /q2\[1\] is 0, not 1/);
});

test("the pk3 builds", () => {
  assert.ok(buildNetQueriesPk3().toString("latin1").includes("maps/MAP01.wad"));
});
