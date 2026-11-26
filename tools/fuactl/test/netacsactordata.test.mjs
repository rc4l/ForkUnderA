import { test } from "node:test";
import assert from "node:assert/strict";
import { STEPS, LATE, DEFAULT, judgeMachine, buildNetActorDataPk3 } from "../scenarios/netacsactordata.mjs";

// [Doruk-Aksoy] Script 1's log as acsactordata wants it, with the full barrel's one warning.
const d1 = `d1 1 3 3 1 -9 0 1 4 1 21 21 0 0 64 0 1 9 5 0\nfua_SetActorData: a ExplosiveBarrel already carries 64 keys, so "k64" was not set.`;

test("script 1 runs everywhere, the server alone sets its value and removes the barrel, and the clients alone read", () => {
  assert.deepEqual(STEPS.map((s) => [s.script, s.clients !== false, s.server !== false]), [[1, true, true], [4, false, true], [5, true, false]]);
  assert.deepEqual(LATE.map((s) => s.script), [5]);
});

test("each machine: its own store, the server's value never on a client, the removed barrel's values gone", () => {
  assert.deepEqual(judgeMachine({ d1, d4: "d4 1" }, { role: "server" }), []);
  assert.match(judgeMachine({ d1, d4: "d4 0" }, { role: "server" }).join(), /took 0/);
  assert.deepEqual(judgeMachine({ d1, d5: `d5 ${DEFAULT} -1 0` }, { role: "player" }), []);
  assert.match(judgeMachine({ d1, d5: "d5 77 -1 0" }, { role: "observer" }).join(), /server's value as 77/);
  assert.match(judgeMachine({ d1, d5: `d5 ${DEFAULT} 9 1` }, { role: "player" }).join(), /read 9, has 1/);
  assert.deepEqual(judgeMachine({ d5: `d5 ${DEFAULT} -1 0` }, { role: "latejoin" }), []);
});

test("the pk3 builds", () => {
  assert.ok(buildNetActorDataPk3().toString("latin1").includes("maps/MAP01.wad"));
});
