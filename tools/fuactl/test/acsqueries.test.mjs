import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeQueries, buildAcsQueriesPk3, waitingScript, FN, FQ, WANT } from "../scenarios/acsqueries.mjs";
import { PCD } from "../src/acs.mjs";

const WARN = "fua_QueryCreate: a script already holds 16 queries; free one with fua_QueryFree.";
const line = (tag, values) => `${tag} ${values.map((v) => (v === null ? 7 : v)).join(" ")}`;
const good = () => Object.fromEntries(Object.entries(WANT).map(([tag, want]) => [tag, tag === "l1" ? `${WARN}\n${line(tag, want)}` : line(tag, want)]));

test("the run passes with every expected value and names each wrong one by index", () => {
  assert.deepEqual(judgeQueries(good()), []);
  assert.match(judgeQueries({ ...good(), q1: line("q1", [0, ...WANT.q1.slice(1)]) }).join(), /q1\[0\] is 0, not an id/);
  assert.match(judgeQueries({ ...good(), s2: "s2 4 13 12" }).join(), /s2\[2\] is 12, not 14/);
  assert.match(judgeQueries({ ...good(), n1: "" }).join(), /script n1 logged nothing/);
  assert.match(judgeQueries({ ...good(), l1: line("l1", WANT.l1) }).join(), /warned 0 times/);
  assert.match(judgeQueries({ ...good(), l1: `${WARN}\n${WARN}\n${line("l1", WANT.l1)}` }).join(), /warned 2 times/);
});

test("the query functions and FQ_ constants keep their numbers", () => {
  assert.deepEqual([FN.QueryCreate, FN.FindActors, FN.FoundCount], [24040, 24045, 24048]);
  assert.deepEqual([FQ.CLASS, FQ.EDGES, FQ.NONE, FQ.NEAREST, FQ.FARTHEST], [1, 16, 0, 32, 33]);
});

test("the waiting script loops on its flag and the pk3 holds the map", () => {
  const words = waitingScript();
  assert.ok(words.includes(PCD.IFGOTO) && words.includes(PCD.GOTO));
  assert.equal(words.at(-1), PCD.TERMINATE);
  assert.ok(buildAcsQueriesPk3().toString("latin1").includes("maps/MAP01.wad"));
});
