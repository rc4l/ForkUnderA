import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeSteps, buildAcsFuncRefPk3, MAPINFO } from "../scenarios/acsfuncref.mjs";
import { readPk3 } from "../src/udmf.mjs";

const PASSING = {
  map01: "FREF same 1\nFREF call 7",
  map02map: "FREF mapstored",
  map02: "FREF stored 7\nFREF storedsame 1",
  map03: "FREF reached\nFunction 0 of ACS library acs/frefmap.o, which this map does not load, called in script 903",
};

test("every step as the fixed engine runs it passes", () => {
  assert.deepEqual(judgeSteps(PASSING), []);
});

test("each fault an older engine shows is named", () => {
  const faults = judgeSteps({ ...PASSING, map01: "FREF same 0\nFREF call 7", map02: "", map03: "FREF reached\nFREF unloaded 7" });
  assert.equal(faults.length, 5);
  assert.match(faults[0], /compares equal to the library's own: logged 0, not 1/);
  assert.match(faults[1], /calls Seven on MAP02: logged nothing/);
  assert.match(faults[3], /ran a function, which returned 7/);
  assert.match(faults[4], /printed no message/);
});

test("scripts that never ran are named", () => {
  const faults = judgeSteps({ ...PASSING, map02map: "", map03: "" });
  assert.ok(faults.includes("MAP02's own script never ran"));
  assert.ok(faults.includes("MAP03's script never ran"));
});

test("the pk3 holds three maps, the three libraries and LOADACS, and no map shows a tally", () => {
  const names = readPk3(buildAcsFuncRefPk3()).map((f) => f.name);
  for (const name of ["maps/MAP01.wad", "maps/MAP02.wad", "maps/MAP03.wad", "acs/freflib.o", "acs/frefuse.o", "acs/frefmap.o", "LOADACS.txt", "mapinfo.txt"])
    assert.ok(names.includes(name), name);
  assert.equal(MAPINFO.match(/nointermission/g).length, 3);
});
