import { test } from "node:test";
import assert from "node:assert/strict";
import { buildAcsBusyPk3, buildScripts, busyLoop, BITS, COUNTER, PASSES } from "../scenarios/acsbusy.mjs";
import { judgeCounter } from "../src/template.mjs";
import { PCD, SCRIPTF } from "../src/acs.mjs";

test("the busy script carries the BUSY flag and the plain one none", () => {
  const [busy, plain] = buildScripts();
  assert.equal(busy.flags, SCRIPTF.BUSY);
  assert.equal(plain.flags, undefined);
});

test("each loop runs well past the runaway limit, five p-codes a pass", () => {
  assert.ok(5 * PASSES > 2000000);
  const words = busyLoop(3, 0);
  assert.deepEqual(words, [PCD.INCMAPVAR, 3, PCD.PUSHMAPVAR, 3, PCD.PUSHNUMBER, PASSES, PCD.GE, PCD.IFNOTGOTO, 8, PCD.TERMINATE]);
});

test("the plain script's loop jumps back into its own code, laid after the busy one's", () => {
  const [{ code: busy }, { code: plain }] = buildScripts();
  const jump = plain[plain.indexOf(PCD.IFNOTGOTO) + 1];
  const loopWord = (jump - 8) / 4 - busy.length;
  assert.equal(plain[loopWord], PCD.INCMAPVAR);
  assert.equal(plain[loopWord + 1], 1, "its own map variable");
});

test("the map carries both scripts and the counter", () => {
  const text = buildAcsBusyPk3().toString("latin1");
  assert.ok(text.includes("ACSE"));
  assert.ok(text.includes("SFLG"));
  assert.ok(text.includes("server int zx_acsbusy_passed"));
});

test("only the busy script's end and the plain script's start pass", () => {
  const value = (v) => judgeCounter("MAP01", `"zx_acsbusy_passed" is "${v}"`, COUNTER).failed;
  assert.equal(value(BITS.busyEnded + BITS.plainRan), 0);
  assert.equal(value(BITS.plainRan), 1, "the busy script terminated as a runaway");
  assert.equal(value(BITS.busyEnded + BITS.plainRan + BITS.plainEnded), 1, "the plain script ran past the limit");
});
