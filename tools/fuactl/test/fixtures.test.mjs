import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { buildGameThreadFixtureWad, FIGHT, FIXTURES, fixtureDir, fixtureFiles, fixtureIwad, pickFixtures, stepAt } from "../src/fixtures.mjs";
import { FREEDOOM2 } from "../src/template.mjs";
import { parseUdmf, readWad } from "../src/udmf.mjs";
import { SPECIAL, THING } from "../src/doomnums.mjs";

test("the generated fixture holds every ingredient a tic changes for the renderer to read", () => {
  const lumps = readWad(buildGameThreadFixtureWad());
  const names = lumps.map((l) => l.name);
  for (const n of ["ANIMDEFS", "MAP01", "TEXTMAP", "BEHAVIOR"]) assert.ok(names.includes(n), n);
  const map = parseUdmf(lumps.find((l) => l.name === "TEXTMAP").data.toString("latin1"));
  const types = map.thing.map((t) => t.type);
  for (const t of ["InvulnerabilitySphere", "Infrared", "RadSuit", "Berserk", "RocketLauncher", "DoomImp", "Zombieman", "ShotgunGuy", "Demon", "PointLight", "MapSpot"])
    assert.ok(types.includes(THING[t]), t);
  const specials = map.linedef.map((l) => l.special);
  assert.ok(specials.includes(SPECIAL.Scroll_Texture_Left));
  assert.ok(specials.includes(SPECIAL.Polyobj_StartLine));
  const script = lumps.find((l) => l.name === "BEHAVIOR").data.toString("latin1");
  for (const s of ["FUAFIX", "fixture hud", "echo fixture console"]) assert.ok(script.includes(s), s);
});

test("fixtures are picked by name, all of them by default", () => {
  assert.equal(pickFixtures().length, FIXTURES.length);
  assert.deepEqual(pickFixtures(["gtfix"]).map((f) => f.name), ["gtfix"]);
  assert.throws(() => pickFixtures(["nope"]), /no fixture nope/);
  assert.equal(fixtureDir("/h"), path.join("/h", ".forkundera", "fixtures"));
});

test("a fixture's files are its generated wad, or its pwads found by name in the store", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "fixtures-"));
  const gen = fixtureFiles(FIXTURES[0], dir, null);
  assert.deepEqual(gen.missing, []);
  assert.ok(fs.statSync(gen.files[0]).size > 0);
  const sunder = FIXTURES.find((f) => f.name === "sunder-map11");
  assert.deepEqual(fixtureFiles(sunder, dir, dir, []).missing, ["sunder_2512.wad"]);
  assert.deepEqual(fixtureFiles(sunder, dir, "/store", ["Sunder_2512.WAD"]).files, [path.join("/store", "Sunder_2512.WAD")]);
  assert.deepEqual(fixtureFiles({ name: "stock" }, dir, "/store", []).files, []);
});

test("the fight's step at a time is the last one due", () => {
  assert.equal(stepAt(FIGHT, 0), FIGHT[0]);
  assert.equal(stepAt(FIGHT, 100), FIGHT[1]);
  assert.equal(stepAt(FIGHT, 10000), FIGHT[FIGHT.length - 1]);
  assert.equal(stepAt([{ at: 5 }], 0), null);
});

test("a fixture's IWAD is Freedoom from the repo, or found by name in the store or beside the engine", () => {
  assert.equal(fixtureIwad(FIXTURES[0], "/s", "/e"), FREEDOOM2);
  const sunder = FIXTURES.find((f) => f.name === "sunder-map11");
  assert.equal(fixtureIwad(sunder, "/s", "/e", (p) => p === path.join("/e", "doom2.wad")), path.join("/e", "doom2.wad"));
  assert.equal(fixtureIwad(sunder, "/s", "/e", (p) => p === path.join("/s", "doom2.wad")), path.join("/s", "doom2.wad"));
  assert.throws(() => fixtureIwad(sunder, "/s", "/e", () => false), /needs doom2.wad/);
});
