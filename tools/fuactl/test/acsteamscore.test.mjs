import { test } from "node:test";
import assert from "node:assert/strict";
import { WANT, SIDES, TEAM, parseScores, judgeScores, buildTeamScoreWad } from "../scenarios/acsteamscore.mjs";
import { STEPS, LATE, CONFIGS, judgeMachine, buildNetTeamScoreWad } from "../scenarios/netacsteamscore.mjs";
import { SNAP_SERVER_CVARS } from "../src/netplay.mjs";

test("a score log reads as its numbers, and a wrong or missing one is a fault", () => {
  assert.deepEqual(parseScores("x\nts 1 1 0 0 7 3\n", "ts"), [1, 1, 0, 0, 7, 3]);
  assert.equal(parseScores("nothing", "tg"), null);
  assert.deepEqual(judgeScores("ts 1 1 0 0 7 3", "ts"), []);
  assert.match(judgeScores("tg 0 0", "tg").join(), /tg logged 0 0, not 7 3/);
  assert.match(judgeScores("", "ts").join(), /script ts logged nothing/);
});

test("both teams are set once, a repeat and no team refused, in every team mode with its limit off", () => {
  assert.deepEqual(WANT, { ts: [1, 1, 0, 0, 7, 3], tg: [7, 3] });
  assert.deepEqual([TEAM.blue, TEAM.red], [0, 1]);
  assert.deepEqual(SIDES.map((s) => s.name), ["teamdm", "ctf", "teamlms"]);
  assert.equal(SIDES.find((s) => s.name === "ctf").launchCvars.pointlimit, 0);
  assert.ok(buildTeamScoreWad().length > 0);
});

test("the server sets the scores alone, then every machine reads them, the late joiner too", () => {
  assert.deepEqual(STEPS.map((s) => [s.script, s.clients !== false]), [[1, false], [2, true]]);
  assert.deepEqual(LATE.map((s) => s.script), [2]);
  assert.deepEqual(judgeMachine({ ts: "ts 1 1 0 0 7 3", tg: "tg 7 3" }, { role: "server" }), []);
  assert.deepEqual(judgeMachine({ tg: "tg 7 3" }, { role: "observer" }), []);
  assert.match(judgeMachine({ tg: "tg 0 0" }, { role: "latejoin" }).join(), /not 7 3/);
  for (const c of CONFIGS) assert.deepEqual({ ...SNAP_SERVER_CVARS, ...c.serverCvars }, c.serverCvars);
  assert.deepEqual(CONFIGS.map((c) => c.name), ["teamdm", "ctf"]);
  assert.ok(buildNetTeamScoreWad().length > buildTeamScoreWad().length);
});
