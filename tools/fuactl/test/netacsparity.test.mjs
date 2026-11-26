import { test } from "node:test";
import assert from "node:assert/strict";
import { CHECKS } from "../scenarios/acsparity.mjs";
import { ANSWERS, STEPS, STARTS, wantFor, judgeMachine, buildNetParityPk3 } from "../scenarios/netacsparity.mjs";

// [Doruk-Aksoy] A log line of every check's answer for `role`, as script 1 prints it.
const lineFor = (role) => `parity ${CHECKS.map((c) => { const v = wantFor(c, role); return c.str ? `<${v}>` : v; }).join(" ")}\n`;

test("every machine's own answer names a check acsparity has", () => {
  const whats = new Set(CHECKS.map((c) => c.what));
  for (const [role, answers] of Object.entries(ANSWERS)) for (const what of Object.keys(answers)) assert.ok(whats.has(what), `${role}: ${what}`);
});

test("a client leaves the barrel at 20, the late joiner reads the server's 10 and step height 40, and the server single player's answers", () => {
  const damage = CHECKS.find((c) => c.what === "DamageActor(barrel, 10, Fire)");
  const health = CHECKS.find((c) => c.what === "the barrel's health after");
  assert.deepEqual(["server", "player", "observer", "latejoin"].map((r) => [wantFor(damage, r), wantFor(health, r)]), [[10, 10], [-1, 20], [-1, 20], [-1, 10]]);
  assert.equal(wantFor(CHECKS.find((c) => c.what === "APROP_MaxStepHeight"), "latejoin"), 40 * 65536);
});

test("each machine is judged against its own answers and DamageActor's -1 on a missing tid", () => {
  for (const role of ["server", "player", "observer", "latejoin"]) assert.deepEqual(judgeMachine({ parity: lineFor(role), damagenull: "damagenull -1" }, { role }), [], role);
  const player = judgeMachine({ parity: lineFor("server"), damagenull: "damagenull -1" }, { role: "player" });
  assert.ok(player.includes("DamageActor(barrel, 10, Fire) is 10, not -1"), player.join());
  assert.match(judgeMachine({ damagenull: "damagenull -1" }, { role: "player" }).join(), /logged no checks/);
  assert.match(judgeMachine({ parity: "parity 1 2", damagenull: "damagenull -1" }, { role: "player" }).join(), /logged 2 values/);
});

test("the pk3 runs scripts 1 and 2 through the copies, with starts for three more players", () => {
  assert.deepEqual(STEPS.map((s) => s.script), [1, 2]);
  assert.equal(STARTS.length, 3);
  assert.ok(buildNetParityPk3().toString("latin1").includes("maps/MAP01.wad"));
});
