import { test } from "node:test";
import assert from "node:assert/strict";
import { SCRIPTF } from "../src/acs.mjs";
import { FN, SCRIPTS, STRINGS, logged, judgeSteps, buildAcsRandomPk3 } from "../scenarios/acsrandom.mjs";
import { STEPS, judgeMachines, buildNetRandomPk3 } from "../scenarios/netacsrandom.mjs";

const good = { first: [1, 2], again: [1, 2], next: [3, 4], nextAfterClient: [3, 4], global: [5], globalAfterNamed: [5], oldGlobal: [6], oldGlobalAfterNamed: [6],
  fixed: [0, 32768, 65536, 131072], empty: [0] };

test("the functions sit at 24070-24072, script 3 is CLIENTSIDE, and the empty name is one of the strings", () => {
  assert.deepEqual(FN, { Random: 24070, FRandom: 24071, SetRandomSeed: 24072 });
  assert.equal(SCRIPTS[3].flags, SCRIPTF.CLIENTSIDE);
  assert.ok(STRINGS.includes(""));
  assert.ok(buildAcsRandomPk3().toString("latin1").includes("maps/MAP01.wad"));
});

test("a log's numbers come after its tag at a line's start", () => {
  assert.deepEqual(logged("x\nr1 4 -5 6\n", "r1"), [4, -5, 6]);
  assert.equal(logged("xr1 4", "r1"), null);
  assert.equal(logged("", "r1"), null);
});

test("each check fails on its own fault", () => {
  assert.deepEqual(judgeSteps(good), []);
  const fault = (patch, re) => assert.match(judgeSteps({ ...good, ...patch }).join(), re);
  fault({ again: [9, 9] }, /seeded the same twice/);
  fault({ nextAfterClient: [7, 7] }, /CLIENTSIDE script's draws moved/);
  fault({ next: [1, 2], nextAfterClient: [1, 2] }, /gave its first five again/);
  fault({ globalAfterNamed: [8] }, /moved Random\(\)'s sequence/);
  fault({ oldGlobalAfterNamed: null }, /with compat_oldrandom on/);
  fault({ fixed: [0, 70000, 1, 131072] }, /fixed draws read/);
  fault({ fixed: [0, 1, 2, 3] }, /fixed draws read/);
  fault({ empty: [4] }, /empty name gave 4/);
});

test("netplay: the clients must agree with each other, and the server's seeded run repeat", () => {
  const m = (role) => ({ role });
  const server = m("server"), a = m("player"), b = m("observer"), c = m("latejoin");
  const logs = (r1, r2) => ({ r1: `r1 ${r1}`, r2: `r2 ${r2}` });
  const first = new Map([[a, logs("1 2", "3 4")], [b, logs("1 2", "3 4")], [c, logs("1 2", "3 4")], [server, logs("7 8", "9 9")]]);
  const again = new Map([[server, logs("7 8", "")]]);
  assert.deepEqual(judgeMachines(first, again), []);
  assert.match(judgeMachines(new Map([...first, [c, logs("1 5", "3 4")]]), again).join(), /late joiner's r1 1,5 isn't the player's client's 1,2/);
  assert.match(judgeMachines(new Map([...first, [b, { r1: "" , r2: "r2 3 4" }]]), again).join(), /observer's client logged no r1/);
  assert.match(judgeMachines(first, new Map([[server, logs("6 6", "")]])).join(), /server's seeded stream gave 7,8, then 6,6/);
  assert.deepEqual(STEPS.map((s) => s.script), [1, 2]);
  assert.ok(buildNetRandomPk3().toString("latin1").includes("maps/MAP01.wad"));
});
