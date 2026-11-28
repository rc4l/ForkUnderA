import { test } from "node:test";
import assert from "node:assert/strict";
import { buildResetSavedMap, buildResetSavedPk3, judgeCounters, CHECKS, READ, APROP, DEATH_ARG, KEPT_TID, KILLED_TID, MAP_VALUES, STATUE_ED } from "../scenarios/resetsaved.mjs";
import { parseUdmf, readPk3 } from "../src/udmf.mjs";
import { PCD } from "../src/acs.mjs";
import { SPECIAL } from "../src/doomnums.mjs";

test("both statues carry every map value, and only the kept one a special", () => {
  const things = parseUdmf(buildResetSavedMap().textmap()).thing.filter((t) => t.type === STATUE_ED);
  assert.deepEqual(things.map((t) => t.id), [KILLED_TID, KEPT_TID]);
  for (const t of things) for (const [key, value] of Object.entries(MAP_VALUES)) assert.equal(t[key], value, `${t.id} ${key}`);
  assert.equal(things[0].special, undefined);
  assert.equal(things[1].special, SPECIAL.ACS_Execute);
  assert.equal(things[1].arg2, DEATH_ARG);
});

test("each check reads its statue's property, user variable or tid count", () => {
  assert.deepEqual(READ.prop([KEPT_TID, APROP.score]), [PCD.PUSHNUMBER, KEPT_TID, PCD.PUSHNUMBER, APROP.score, PCD.GETACTORPROPERTY]);
  assert.deepEqual(READ.count(99), [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 99, PCD.THINGCOUNT]);
  assert.ok(CHECKS.every(([, kind]) => READ[kind]));
  // [Doruk-Aksoy] One bit a check above the ran bit, all inside an int.
  assert.ok(CHECKS.length < 30);
});

test("a run passes only with the checks clean and the special's argument back", () => {
  assert.deepEqual(judgeCounters(1, DEATH_ARG), []);
  assert.deepEqual(judgeCounters(null, DEATH_ARG), ["the check script never ran"]);
  assert.match(judgeCounters(1 + 2 ** 6, DEATH_ARG)[0], /score/);
  assert.match(judgeCounters(1, 1)[0], /ran its special with 1, not 7/);
  assert.match(judgeCounters(1, null)[0], /with nothing/);
});

test("the pk3 carries the map, its scripts, the statue and both counters", () => {
  const names = readPk3(buildResetSavedPk3()).map((e) => e.name);
  for (const name of ["maps/MAP01.wad", "decorate.txt", "cvarinfo.txt"]) assert.ok(names.includes(name), name);
});
