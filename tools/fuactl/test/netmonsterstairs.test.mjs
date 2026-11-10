// [Doruk-Aksoy] The hosted monster stairs scenario's verdicts: the cvar reaching each client, the counter, and each client's enemies during the climb.
import { test } from "node:test";
import assert from "node:assert/strict";
import { stairCorpses, stairWalkers, WAVE_ON_STEPS, WAVE_WAIT_MS, readCvarSettled, cvarOn, counterValue, climbVerdict, cvarVerdict, boxFloor, sunkEnemies, sunkVerdict, RADIUS, CONFIGS, MAPS } from "../scenarios/netmonsterstairs.mjs";
import { MAPS as STAIR_MAPS } from "../scenarios/monsterstairs.mjs";

const CLASSES = ["DoomImp", "Fatso", "Arachnotron"];

test("the cvar reads on from a bool or an int answer, and off otherwise", () => {
  assert.ok(cvarOn('"sv_fua_monsterstairs" is "true"'));
  assert.ok(cvarOn('"sv_fua_monsterstairs" is "1"'));
  assert.ok(!cvarOn('"sv_fua_monsterstairs" is "false"'));
  assert.ok(!cvarOn("Unknown command"));
});

test("a client without the cvar is named, and all with it pass", () => {
  const on = { who: "player player 1", text: '"sv_fua_monsterstairs" is "true"' };
  const off = { who: "observer player 2", text: '"sv_fua_monsterstairs" is "false"' };
  assert.deepEqual(cvarVerdict("MAP05 lan", [on, off]), { line: "FAIL MAP05 lan: sv_fua_monsterstairs never reached observer player 2", failed: 1 });
  assert.equal(cvarVerdict("MAP05 lan", [on]).failed, 0);
});

test("the counter is read as a number, negative included, or null when absent", () => {
  assert.equal(counterValue('"zx_stairs_wrong" is "3"'), 3);
  assert.equal(counterValue('"zx_stairs_wrong" is "-1"'), -1);
  assert.equal(counterValue("nothing"), null);
});

// [Doruk-Aksoy] Walking offsets reach 72 units on a flat floor, so they are reported; an enemy missing or at another health on a client is a desync and fails.
test("during the climb a client fails on an enemy missing or hurt differently, and reports each enemy's worst offset", () => {
  const round1 = [{ kind: "actor", what: "Arachnotron 9 20.00 units apart", apart: 20 }, { kind: "actor", what: "Fatso 8 facing 90 on the server, 45 on the client" }];
  const round2 = [{ kind: "actor", what: "Arachnotron 9 72.00 units apart", apart: 72 }, { kind: "actor", what: "DoomImp 7 8.00 units apart", apart: 8 }];
  assert.deepEqual(climbVerdict("MAP05 lan player 1", [round1, round2], CLASSES),
    { line: "PASS MAP05 lan player 1: 2 comparisons during the climb, every enemy present at its health; worst offsets Arachnotron 9 72, DoomImp 7 8", failed: 0 });
  assert.equal(climbVerdict("x", [[{ kind: "actor", what: "Fatso 8 only on the server" }]], CLASSES).failed, 1);
  assert.equal(climbVerdict("x", [[{ kind: "actor", what: "Fatso 8 health 600 on the server, 500 on the client" }]], CLASSES).failed, 1);
  assert.deepEqual(climbVerdict("x", [], CLASSES), { line: "FAIL x: never compared during the climb", failed: 1 });
  assert.match(climbVerdict("x", [[]], CLASSES).line, /worst offsets none apart/);
  // [Doruk-Aksoy] Only the enemies are judged: a projectile exists a tic apart on a client by design, and an ArachnotronPlasma is not an Arachnotron.
  assert.equal(climbVerdict("x", [[{ kind: "actor", what: "ArachnotronPlasma 29 only on the server" }, { kind: "actor", what: "DoomPlayer 2 only on the client" }]], CLASSES).failed, 0);
});

test("it hosts the stock enemies' maps with the cvar off at launch, a player and an observer in each config", () => {
  assert.deepEqual(MAPS, ["MAP05", "MAP06", "MAP07"]);
  assert.ok(STAIR_MAPS.MAP07.control, "the flat walk every offset is set against");
  for (const m of MAPS) assert.ok(STAIR_MAPS[m].enemies, m);
  assert.deepEqual(CONFIGS.map((c) => c.name), ["lan", "ping100", "lossy-observer"]);
  for (const c of CONFIGS) {
    assert.equal(c.serverCvars.sv_fua_monsterstairs, 0, "on only after the clients joined, so it reaches them as a change");
    assert.deepEqual([c.players, c.observers], [1, 1]);
  }
});

// [Doruk-Aksoy] The floor a box covers on MAP05's flight of 16-deep, 16-high steps from -128 up to the landing at 0.
test("the floor under a box is the highest step it covers, and none when an edge sits on a step", () => {
  const floors = STAIR_MAPS.MAP05.floors;
  assert.equal(boxFloor(floors, 256, 64), -128, "in the hall");
  assert.equal(boxFloor(floors, 498.7, 64), -64, "the dead Arachnotron the server held at -64");
  assert.equal(boxFloor(floors, 533, 64), -32);
  assert.equal(boxFloor(floors, 900, 64), 0, "on the landing");
  assert.equal(boxFloor(floors, 496, 64), null, "its north edge on the 560 step");
  assert.equal(boxFloor(STAIR_MAPS.MAP07.floors, 512, 64), 0, "a flat line between equal floors decides nothing");
  assert.deepEqual(RADIUS, { DoomImp: 20, Fatso: 48, Arachnotron: 64 });
});

// [Doruk-Aksoy] From floortrace: clients held a climbing Arachnotron on the floor under its centre (-96) with its box over the -32 step, falling from -49.
test("an enemy on a floor below both its box floor and its height is sunk; one at its height or falling to its box floor is not", () => {
  const floors = STAIR_MAPS.MAP05.floors;
  const sunk = sunkEnemies([{ c: "Arachnotron", netid: 26, x: 123, y: 533, z: -49, fz: -96, health: 500 }], floors);
  assert.equal(sunk.length, 1);
  assert.match(sunk[0], /Arachnotron 26 at y 533 on floor -96 under a box floor of -32 at z -49/);
  assert.deepEqual(sunkEnemies([
    { c: "Arachnotron", netid: 26, y: 533, z: -49, fz: -49, health: 500 },
    { c: "Arachnotron", netid: 27, y: 533, z: 0, fz: -32, health: 500 },
    { c: "Arachnotron", netid: 28, y: 498.7, z: -64, fz: -64, health: 0 },
    { c: "DoomPlayer", netid: 2, y: 533, z: -128, fz: -128, health: 100 },
  ], floors), []);
  assert.match(sunkEnemies([{ c: "Arachnotron", netid: 28, y: 498.7, z: -100, fz: -128, health: 0 }], floors)[0], /Arachnotron 28 \(dead\)/);
  // [Doruk-Aksoy] Once it has landed on the floor under its centre it is at its own floor again; the exact comparison with the server after the kill is what sees that.
  assert.deepEqual(sunkEnemies([{ c: "Arachnotron", netid: 28, y: 498.7, z: -128, fz: -128, health: 0 }], floors), []);
  assert.equal(sunkVerdict("x", []).failed, 0);
  assert.equal(sunkVerdict("x", ["a", "a"]).failed, 1);
  assert.match(sunkVerdict("x", ["a", "a"]).line, /2 time\(s\)/);
});

// [Doruk-Aksoy] On a link losing a tenth of its packets the observer held the cvar off 20 tics after the change, and on by the late join.
test("the cvar is read again until every client holds it on, and given up on after the time allowed", async () => {
  const on = { who: "a", text: '"sv_fua_monsterstairs" is "true"' }, off = { who: "b", text: '"sv_fua_monsterstairs" is "false"' };
  let reads = 0, clock = 0;
  const later = await readCvarSettled({}, 5000, { read: async () => (++reads < 3 ? [on, off] : [on, on]), sleep: async (ms) => { clock += ms; }, now: () => clock });
  assert.equal(reads, 3);
  assert.equal(cvarVerdict("x", later).failed, 0);
  reads = 0; clock = 0;
  const never = await readCvarSettled({}, 1000, { read: async () => { reads++; return [on, off]; }, sleep: async (ms) => { clock += ms; }, now: () => clock });
  assert.equal(cvarVerdict("x", never).failed, 1);
  assert.equal(reads, 11, "every 100 ms until the second has gone");
});

// [Doruk-Aksoy] A late joiner spawned corpses on the floor under their centre; only corpses lying on the steps show that, so the server must hold some there.
test("a corpse on the stairs lies between the hall's floor and the landing's, and only a dead enemy counts", () => {
  const floors = STAIR_MAPS.MAP06.floors;
  const found = stairCorpses([
    { c: "Arachnotron", netid: 1, z: -64, health: 0 }, { c: "Fatso", netid: 2, z: -32, health: -10 },
    { c: "Arachnotron", netid: 3, z: 0, health: 0 }, { c: "Arachnotron", netid: 4, z: -128, health: 0 },
    { c: "Fatso", netid: 5, z: -32, health: 600 }, { c: "DoomPlayer", netid: 6, z: -64, health: 0 },
  ], floors);
  assert.deepEqual(found.map((a) => a.netid), [1, 2]);
  assert.equal(stairCorpses([{ c: "Fatso", netid: 2, z: 0, health: 0 }], STAIR_MAPS.MAP07.floors).length, 0, "the flat control has no stairs");
  assert.ok(WAVE_ON_STEPS >= 1 && WAVE_ON_STEPS <= 3 && WAVE_WAIT_MS > 0);
  // [Doruk-Aksoy] MAP05's wave starts in the hall: killed 22 tics in, as MAP06's placed one is, none had reached the steps.
  assert.deepEqual(stairWalkers([
    { c: "Arachnotron", netid: 1, z: -96, health: 500 }, { c: "Fatso", netid: 2, z: -128, health: 600 },
    { c: "DoomImp", netid: 3, z: 0, health: 60 }, { c: "Arachnotron", netid: 4, z: -64, health: 0 },
  ], STAIR_MAPS.MAP05.floors).map((a) => a.netid), [1]);
});
