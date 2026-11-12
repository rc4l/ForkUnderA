import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeJumps, gapsIn, buildScripts, buildNetRejumpDelayPk3, CONFIGS, GAPS } from "../scenarios/netrejumpdelay.mjs";

const SLOW = CONFIGS.find((c) => c.name === "decorate");

test("the gaps a log holds drop the standing start", () => {
  assert.deepEqual(gapsIn("server ground 40\nclient ground 3\nserver ground 30\nserver ground 31", "server ground"), [30, 31]);
  assert.deepEqual(gapsIn("", "client ground"), []);
});

test("a configuration passes when the delay reads back and both sides wait it within a tic", () => {
  assert.deepEqual(judgeJumps({ set: "set 30 1", server: [30, 31], client: [29, 30] }, SLOW), []);
  assert.deepEqual(judgeJumps({ set: "set -1 1", server: [7, 7], client: [7, 7] }, CONFIGS[0]), []);
});

test("a wrong wait, a missing read-back or too few jumps are each named", () => {
  assert.deepEqual(judgeJumps({ set: "set 30 1", server: [7, 7], client: [30, 30] }, SLOW), ["the server stood 7 and 7 tics between jumps, not 30"]);
  assert.deepEqual(judgeJumps({ set: "set 0 0", server: [30, 30], client: [30, 30] }, SLOW), ["RejumpDelay read back 0 and checked 0, not 30 and 1"]);
  assert.deepEqual(judgeJumps({ set: "", server: [30], client: [30, 30] }, SLOW), ["setting RejumpDelay 30 logged nothing", `the server logged 1 gaps between jumps, not ${GAPS - 1}`]);
});

test("the map carries a server loop, a client loop and the setter, and only the DECORATE config the slow class", () => {
  assert.deepEqual(buildScripts().map((s) => s.number), [1, 2, 3]);
  assert.ok(buildNetRejumpDelayPk3().toString("latin1").includes("maps/MAP01.wad"));
  assert.ok(!buildNetRejumpDelayPk3().toString("latin1").includes("decorate.txt"));
  assert.ok(SLOW.wad.toString("latin1").includes("decorate.txt") && !SLOW.set);
});
