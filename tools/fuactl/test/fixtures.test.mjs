import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { buildGameThreadFixtureWad, buildGameThreadModWad, gtmodScript, CHECKS, comparePicture, FIGHT, FIXTURES, fixtureDir, fixtureFiles, fixtureIwad, hudTics, loadRefusal, parseCopyReport, parsePrescanReport, prescanCheckFixture, parsePSpriteReport, parseSeamReport, parseSeenReport, parseSpriteReport, parseVisReport, pickFixtures, stepAt, sumReports } from "../src/fixtures.mjs";
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

test("the sprite report counts the actors derived beside GL and those that differed", () => {
  const r = parseSpriteReport("sprites: 412 actors derived beside GL (36 model surfaces), 2 differed (5 only ours, 1 only GL's)\n  ZombieMan at (1, 2): light\n");
  assert.deepEqual([r.walked, r.models, r.differed, r.found], [412, 36, 2, true]);
  assert.deepEqual([parseSpriteReport("").found, parseSpriteReport("").walked], [false, 0]);
});

test("every fixture check is one entry the command line picks by name", () => {
  assert.deepEqual(Object.keys(CHECKS).sort(), ["copycheck", "hudcheck", "prescancheck", "pspritecheck", "seamcheck", "seencheck", "spritecheck", "vischeck"]);
  for (const run of Object.values(CHECKS)) assert.equal(typeof run, "function");
});

test("reports from several views add up, any one finding something counting", () => {
  const r = sumReports([{ walked: 3, models: 1, differed: 0, found: true, text: "a" }, { walked: 2, differed: 1, found: false, text: "" }]);
  assert.deepEqual(r, { walked: 5, models: 1, differed: 1, found: true, text: "a" });
  assert.deepEqual(sumReports([]), { walked: 0, differed: 0, found: false, text: "" });
});

test("the weapon report counts the frames derived beside GL and those that differed", () => {
  const r = parsePSpriteReport("weapon: 120 frames derived beside GL (240 quads), 1 differed\n  quad: GL 2 quads\n");
  assert.deepEqual([r.walked, r.quads, r.differed, r.found], [120, 240, 1, true]);
  assert.equal(parsePSpriteReport("").found, false);
});

test("the seen report counts the lines and sectors the walks marked on the live map", () => {
  const r = parseSeenReport("seen: 312 lines mapped, 40 sectors drawn");
  assert.deepEqual([r.walked, r.sectors, r.differed, r.found], [312, 40, 0, true]);
  assert.deepEqual([parseSeenReport("").found, parseSeenReport("").walked], [false, 0]);
});

test("the seam report counts the seams entered and takes the total violations, not a seam's own", () => {
  const text = ["gc/object                        game data          235 entries        1 violations",
    "copy/fill                        handoff            322 entries        0 violations",
    "1 violations, the first in gc/object on the another thread; 0 locks taken while holding another; 0 seams declared with a second owner",
    "  violated gc/object in DObject:456 by the main thread, no tic running, main thread drawing: 1"].join("\n");
  const r = parseSeamReport(text);
  assert.deepEqual([r.walked, r.differed, r.first, r.found], [2, 1, "gc/object (another)", true]);
  assert.deepEqual(r.sites, ["gc/object in DObject:456 by the main thread, no tic running, main thread drawing: 1"]);
  const clean = parseSeamReport("copy/fill  handoff  3 entries  0 violations\n0 violations; 0 locks taken while holding another");
  assert.deepEqual([clean.walked, clean.differed, clean.first, clean.sites], [1, 0, "", []]);
  assert.equal(parseSeamReport("1 violations, the first in sound/channels on the main thread in S_StartSound:1234; 0 locks").first, "sound/channels (main, S_StartSound:1234)");
  assert.equal(parseSeamReport("Unknown command").found, false);
});

test("the HUD check pauses at the picture tic and the half second before it", () => {
  assert.deepEqual(hudTics({ tic: 320 }), [290, 305, 320]);
});

test("gtmod carries a mod's lumps, two maps and its probe script", async () => {
  const { readWad } = await import("../src/udmf.mjs");
  const names = readWad(buildGameThreadModWad()).map((l) => l.name);
  for (const n of ["MAPINFO", "CVARINFO", "KEYCONF", "DECORATE", "GLDEFS", "SBARINFO", "ANIMDEFS", "MAP01", "BEHAVIOR", "MAP02"]) assert.ok(names.includes(n), n);
  assert.equal(gtmodScript().toString("latin1", 0, 4), "ACS\0");
  assert.ok(FIXTURES.some((f) => f.name === "gtmod" && f.build === buildGameThreadModWad));
});

test("the tic's end scan report gives the fills held to the whole-map scan and what it missed", () => {
  const r = parsePrescanReport("prescan: 412 fills checked, 398 with the actor list, 2 missed\n  side 17 changed after the tic's end scan or past it\n");
  assert.deepEqual([r.walked, r.listed, r.differed, r.found], [412, 398, 2, true]);
  assert.deepEqual([parsePrescanReport("").found, parsePrescanReport("").walked], [false, 0]);
  assert.equal(CHECKS.prescancheck, prescanCheckFixture);
});

// [rc4l] An older engine stopped on a class it did not know and never traced; the line saying so is what savecompat reports.
test("a refused load is told by the last line of the log naming why", () => {
  assert.equal(loadRefusal(["loading", "Unknown class 'FlatDecal'", "more"].join(String.fromCharCode(13, 10))), "Unknown class 'FlatDecal'");
  assert.equal(loadRefusal("Savegame is from an incompatible version: 4525 (4524 is the newest supported)"), "Savegame is from an incompatible version: 4525 (4524 is the newest supported)");
  assert.equal(loadRefusal("all fine"), null);
});
