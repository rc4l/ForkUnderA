import { test } from "node:test";
import assert from "node:assert/strict";
import { CHECKS } from "../scenarios/acsparity.mjs";
import { ANSWERS, STEPS, LATE, STARTS, CLIENT_HEALTHS, wantFor, judgeMachine, godOff, buildNetParityPk3, judgeNetplay } from "../scenarios/netacsparity.mjs";
import { BARREL_AT, ICE } from "../scenarios/acsparity.mjs";

// [Doruk-Aksoy] A log line of every check's answer for `role`, as script 1 prints it.
const lineFor = (role) => `parity ${CHECKS.map((c) => { const v = wantFor(c, role); return c.str ? `<${v}>` : v; }).join(" ")}\n`;
const SERVER_DAMAGE = { sd3: "sectordamage 90 90", sd4: "sectordamage 80 70 70\nSetSectorDamage: sectors here hold only the built-in damage types, so 'ZxPlasma' does normal damage" };
const CLIENT_DAMAGE = { sd3: "sectordamage 100 100", sd4: "sectordamage 90 90 90", sdh: "sdh 70", sc: "sc zxmarine", terr: "terr ZxIce" };
const logsFor = (role) => ({ parity: lineFor(role), damagenull: "damagenull -1", ...(role === "server" ? SERVER_DAMAGE : role === "latejoin" ? { sdh: "sdh 70", sc: "sc zxmarine", terr: "terr ZxIce" } : CLIENT_DAMAGE) });

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

test("each machine is judged against its own answers, DamageActor's -1 on a missing tid, and the sector damage it should see", () => {
  for (const role of ["server", "player", "observer", "latejoin"]) assert.deepEqual(judgeMachine(logsFor(role), { role }), [], role);
  const player = judgeMachine({ ...logsFor("player"), parity: lineFor("server") }, { role: "player" });
  assert.ok(player.includes("DamageActor(barrel, 10, Fire) is 10, not -1"), player.join());
  assert.match(judgeMachine({ damagenull: "damagenull -1" }, { role: "player" }).join(), /logged no checks/);
  assert.match(judgeMachine({ ...logsFor("player"), parity: "parity 1 2" }, { role: "player" }).join(), /logged 2 values/);
});

test("a client's own SetSectorDamage hurts no one, and every client holds the health the server left", () => {
  assert.deepEqual(CLIENT_HEALTHS, { sd3: [100, 100], sd4: [90, 90, 90], after: 70 });
  assert.match(judgeMachine({ ...logsFor("observer"), sd3: "sectordamage 90 90" }, { role: "observer" }).join(), /sd3 copy logged healths 90,90, not 100,100/);
  assert.match(judgeMachine({ ...logsFor("player"), sdh: "sdh 100" }, { role: "player" }).join(), /health at 100, not the server's 70/);
  assert.match(judgeMachine({ ...logsFor("latejoin"), sdh: "" }, { role: "latejoin" }).join(), /at nothing/);
  assert.match(judgeMachine({ ...logsFor("observer"), sc: "sc player" }, { role: "observer" }).join(), /sound class as player, not zxmarine/);
  assert.match(judgeMachine({ ...logsFor("latejoin"), terr: "terr ZxMud" }, { role: "latejoin" }).join(), /terrain as ZxMud, not ZxIce/);
  assert.match(judgeMachine({ ...logsFor("player"), terr: undefined }, { role: "player" }).join(), /terrain as nothing/);
  assert.equal(wantFor(CHECKS.find((c) => c.what === "GetActorFloorTerrain(barrel on mud)"), "latejoin"), "ZxIce");
  assert.match(judgeMachine({ ...logsFor("server"), sd3: "sectordamage 100 100" }, { role: "server" }).join(), /script 3 logged healths 100,100/);
});

test("the player's client turns god mode off, toggling at most twice", async () => {
  const said = ["God mode on.", "God mode off."];
  const sent = [];
  await godOff({ clients: [{ c: "p" }] }, { exec: async (c, t) => { sent.push([c, t]); return said.shift(); } });
  assert.deepEqual(sent, [["p", "god"], ["p", "god"]]);
});

test("the pk3 runs every step through the copies, the late joiner the checks and the read, with starts for three more players", () => {
  assert.deepEqual(STEPS.map((s) => s.script), [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
  assert.deepEqual([7, 8, 9, 10].map((n) => [STEPS.find((s) => s.script === n).clients !== false, STEPS.find((s) => s.script === n).server !== false]),
    [[false, true], [true, false], [false, true], [true, false]]);
  assert.equal(STEPS.find((s) => s.script === 6).clients, false);
  assert.equal(STEPS.find((s) => s.script === 5).server, false);
  assert.deepEqual(LATE.map((s) => s.script), [8, 10, 1, 2, 5]);
  assert.equal(STARTS.length, 3);
  assert.ok(buildNetParityPk3().toString("latin1").includes("maps/MAP01.wad"));
});

test("after the logs, every machine's barrels are read from its own world.actors", async () => {
  const rows = (wetTr) => ({ actors: [{ x: BARREL_AT.far[0], y: BARREL_AT.far[1], tr: ICE }, { x: BARREL_AT.mud[0], y: BARREL_AT.mud[1], tr: ICE },
    { x: BARREL_AT.wet[0], y: BARREL_AT.wet[1], tr: wetTr }, { x: BARREL_AT.net[0], y: BARREL_AT.net[1], tr: ICE }] });
  const member = (role, wetTr) => ({ role, c: { rpc: async () => rows(wetTr) } });
  const set = { server: member("server", 0), clients: [member("player", 0), member("latejoin", ICE)] };
  const passed = { line: "PASS x: fine", failed: 0 };
  assert.equal((await judgeNetplay({ ...set, clients: [set.clients[0]] }, "MAP01 lan", {}, { judge: async () => passed })).failed, 0);
  const r = await judgeNetplay(set, "MAP01 lan", {}, { judge: async () => passed });
  assert.equal(r.failed, 1);
  assert.match(r.line, /the late joiner: the wet barrel's translation is 196615, not 0/);
  const logs = await judgeNetplay({ ...set, clients: [] }, "MAP01 lan", {}, { judge: async () => ({ line: "FAIL MAP01 lan: the server: script 1 logged no checks", failed: 1 }) });
  assert.match(logs.line, /^FAIL MAP01 lan: the server: script 1 logged no checks$/);
});
