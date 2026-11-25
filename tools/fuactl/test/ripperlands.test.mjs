import { test } from "node:test";
import assert from "node:assert/strict";
import { readWad } from "../src/udmf.mjs";
import { buildRipperLandsWad, SIDES, readDeaths, diedInDummy, judgeRipperLands } from "../scenarios/ripperlands.mjs";

test("the wad holds the map, a bouncing ripper that marks its death, a monster dummy and the two downward slices", () => {
  const lumps = readWad(buildRipperLandsWad());
  assert.deepEqual(lumps.map((l) => l.name).slice(0, 2), ["MAP01", "TEXTMAP"]);
  const decorate = lumps.find((l) => l.name === "DECORATE").data.toString("latin1");
  assert.match(decorate, /\+RIPPER\s+Bouncetype Hexen/);
  assert.match(decorate, /Death:\s+TNT1 A 0 A_SpawnItemEx\("ZXRipDied"\)/);
  assert.match(decorate, /actor ZXRipDummy 31117\s+\{\s+\+SOLID\s+\+SHOOTABLE\s+\+ISMONSTER/);
  assert.match(decorate, /A_FireCustomMissile\("ZXRipSlice", 15, 0, -8, -6, 0, -10\)/);
  assert.match(lumps.find((l) => l.name === "TEXTMAP").data.toString("latin1"), /type = 31117;/);
});

test("the side turns monsters on, since the dummy is one", () => {
  assert.equal(SIDES[0].launchCvars.sv_nomonsters, 0);
});

test("readDeaths keeps the markers and sees the dummy", () => {
  const r = readDeaths([{ c: "ZXRipDied", x: 1, y: 2, z: 3 }, { c: "ZXRipDummy" }, { c: "DoomPlayer" }]);
  assert.equal(r.deaths.length, 1);
  assert.equal(r.dummy, true);
  assert.deepEqual(readDeaths(undefined), { deaths: [], dummy: false });
});

test("a death within a slice's reach of the dummy's box is one on the dummy", () => {
  assert.equal(diedInDummy({ x: 250, y: 160, z: 24 }), true);
  assert.equal(diedInDummy({ x: 256, y: 186, z: 24 }), false);
  assert.equal(diedInDummy({ x: 256, y: 164, z: 62 }), false);
});

test("the verdict passes only two deaths away from the dummy", () => {
  const away = { x: 200, y: 500, z: 10 };
  const on = { x: 252, y: 168, z: 24 };
  assert.match(judgeRipperLands("MAP01 vk", { deaths: [], dummy: false }).line, /nothing was tested/);
  assert.deepEqual(judgeRipperLands("MAP01 vk", { deaths: [on, on], dummy: true }),
    { line: "FAIL MAP01 vk: 2 of 2 slices exploded on the monster they came down into", failed: 1 });
  assert.match(judgeRipperLands("MAP01 vk", { deaths: [away], dummy: true }).line, /inconclusive/);
  assert.deepEqual(judgeRipperLands("MAP01 vk", { deaths: [away, away], dummy: true }),
    { line: "PASS MAP01 vk: both slices ripped on through the monster they came down into, dying elsewhere", failed: 0 });
});
