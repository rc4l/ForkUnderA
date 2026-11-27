import { test } from "node:test";
import assert from "node:assert/strict";
import { TRACE_STEPS, WANT } from "../scenarios/acstraces.mjs";
import { logged } from "../scenarios/acshandles.mjs";
import { STEPS, LATE, AFTER, STARTS, WANT_ONLINE, STRINGS_ONLINE, judgeMachine, buildNetTracesPk3 } from "../scenarios/netacstraces.mjs";

test("every step before the savegame runs on every machine and again on the late joiner, a silent one under a tag of its own", () => {
  assert.deepEqual(STEPS.map((s) => s.script), [...TRACE_STEPS.map(([n]) => n), ...STEPS.slice(TRACE_STEPS.length).map((s) => s.script)]);
  assert.ok(STEPS.find((s) => s.script === 11).tags[0].startsWith("held"));
  assert.ok(LATE.every((s) => s.server === false) && LATE.length === STEPS.length);
  assert.ok(Array.isArray(AFTER));
  assert.ok(STARTS.every(([x, y]) => x < 300 && y === 1000), "north-west, behind the imps");
});

test("the online answers are single player's for the tags run online, the savegame's left out", () => {
  for (const tag of Object.keys(WANT_ONLINE)) assert.deepEqual(WANT_ONLINE[tag], WANT[tag]);
  assert.ok(!("v1" in WANT_ONLINE) && !("n1" in WANT_ONLINE));
  assert.ok(Object.keys(STRINGS_ONLINE).every((t) => STEPS.some((s) => s.tags.includes(t))));
});

test("a machine that logged nothing fails, naming the script", () => {
  const faults = judgeMachine({});
  assert.ok(faults.some((f) => /script w1 logged nothing/.test(f)));
  assert.equal(logged("w1 1 2", "w1").length, 2);
});

test("the pk3 builds", () => {
  assert.ok(buildNetTracesPk3().toString("latin1").includes("maps/MAP01.wad"));
});
