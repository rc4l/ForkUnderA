import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { buildGameThreadFixtureWad, comparePicture, FIGHT, FIXTURES, fixtureDir, fixtureFiles, fixtureIwad, parseCopyReport, parseVisReport, pickFixtures, stepAt } from "../src/fixtures.mjs";
import { writePNG } from "../src/pngcore.mjs";
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

test("a capture is saved as its baseline the first time, then held to it by its largest block", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "pictures-"));
  const grey = (v, size = 32) => ({ size, rgb: Buffer.alloc(size * size * 3, v) });
  const file = (name, img) => { const p = path.join(dir, name); writePNG(p, img.size, img.size, img.rgb); return p; };
  const baseline = path.join(dir, "base", "a.png");
  assert.equal(comparePicture(file("a.png", grey(100)), baseline).saved, true);
  assert.equal(comparePicture(file("same.png", grey(100)), baseline).same, true);
  const off = comparePicture(file("off.png", grey(110)), baseline, { tolerance: 2 });
  assert.equal(off.same, false);
  assert.equal(off.max, 10);
  assert.match(off.line, /differs from its baseline: largest block 10.00/);
  assert.equal(comparePicture(file("near.png", grey(101)), baseline, { tolerance: 2 }).same, true);
  assert.match(comparePicture(file("small.png", grey(255, 16)), baseline).line, /16x16, its baseline 32x32/);
  assert.equal(comparePicture(file("new.png", grey(50)), baseline, { update: true }).saved, true);
});

test("the walk check's report is read as frames compared and frames that differed", () => {
  const text = "visibility: 120 frames walked beside GL, 1 differed (2 ids only GL reached, 0 only ours)\nsky: 120 frames derived beside GL, 0 differed\n" +
    "portal views: 40 frames derived beside GL (0 stacked views, 1 mirrors, 0 sheens), 2 differed, 0 with a view GL's occlusion query left unwalked\n";
  assert.deepEqual(parseVisReport(text), { walked: 120, differed: 3, found: true });
  assert.deepEqual(parseVisReport("nothing here"), { walked: 0, differed: 0, found: false });
});

test("the copy check's report is read as structures and blends compared and how many differed", () => {
  const r = parseCopyReport("copy before views: 900 compared, 2 differed\nsector 4's copy is not the live sector\ncopy blends: 30 compared, 1 differed\n");
  assert.deepEqual([r.walked, r.blends, r.differed, r.found], [900, 30, 3, true]);
  assert.deepEqual([parseCopyReport("").found, parseCopyReport("").differed], [false, 0]);
});

test("a fixture's IWAD is Freedoom from the repo, or found by name in the store or beside the engine", () => {
  assert.equal(fixtureIwad(FIXTURES[0], "/s", "/e"), FREEDOOM2);
  const sunder = FIXTURES.find((f) => f.name === "sunder-map11");
  assert.equal(fixtureIwad(sunder, "/s", "/e", (p) => p === path.join("/e", "doom2.wad")), path.join("/e", "doom2.wad"));
  assert.equal(fixtureIwad(sunder, "/s", "/e", (p) => p === path.join("/s", "doom2.wad")), path.join("/s", "doom2.wad"));
  assert.throws(() => fixtureIwad(sunder, "/s", "/e", () => false), /needs doom2.wad/);
});
