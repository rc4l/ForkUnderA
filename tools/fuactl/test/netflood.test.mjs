import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeClients, GAPS, SETTLE_TICS, CHANGES } from "../scenarios/netflood.mjs";

test("the flood's changes fall in the 7-tic window and the steady ones outside every window", () => {
  assert.ok(GAPS.flood <= 7);
  assert.ok(GAPS.steady > 42 && 2 * GAPS.steady > 105);
  assert.ok(SETTLE_TICS > 105);
  assert.equal(new Set(CHANGES).size, CHANGES.length);
});

test("a flooder must be gone and a steady changer kept, with the observer kept either way", () => {
  const both = [{ player: 0 }, { player: 1 }], observerOnly = [{ player: 1 }];
  assert.deepEqual(judgeClients(observerOnly, 0, 1, true), []);
  assert.deepEqual(judgeClients(both, 0, 1, false), []);
  assert.deepEqual(judgeClients(both, 0, 1, true), ["kept player 0, who flooded"]);
  assert.deepEqual(judgeClients(observerOnly, 0, 1, false), ["kicked player 0, who changed steadily"]);
  assert.deepEqual(judgeClients(undefined, 0, 1, true), ["dropped the observer, player 1"]);
});
