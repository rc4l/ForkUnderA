import { test } from "node:test";
import assert from "node:assert/strict";
import { shownLevel, judgeNext, MAPINFO, buildSkirmishLevelsPk3 } from "../scenarios/skirmishlevels.mjs";

test("the level row's map is read from its text", () => {
  assert.equal(shownLevel([{ text: "SKIRMISH" }, { text: "Level MAP03 - Three" }]), "MAP03");
  assert.equal(shownLevel([{ text: "SKIRMISH" }]), null);
});

test("MAP03 after MAP01 passes, MAP02 or nothing is named", () => {
  assert.deepEqual(judgeNext("MAP03"), []);
  assert.deepEqual(judgeNext("MAP02"), ["the level after MAP01 is MAP02, not MAP03"]);
  assert.deepEqual(judgeNext(null), ["the menu showed no level after MAP01"]);
});

test("only MAP02 is marked, and the pk3 holds all three maps", () => {
  assert.equal(MAPINFO.match(/noskirmish/g).length, 1);
  assert.match(MAPINFO, /"Two"\n\{\n  noskirmish/);
  const bytes = buildSkirmishLevelsPk3().toString("latin1");
  for (const n of ["MAP01", "MAP02", "MAP03"]) assert.ok(bytes.includes(`maps/${n}.wad`), n);
});
