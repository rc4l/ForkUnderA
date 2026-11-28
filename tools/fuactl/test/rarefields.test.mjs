import { test } from "node:test";
import assert from "node:assert/strict";
import { buildRareFieldsMap, buildRareFieldsPk3, judgeCounter, CHECKS, READ, DECORATE, ED, TID, PARENT, CHILD, HEIR, S, GROWN, GROWN_DECORATE, GROWN_TIDS, grownName, SKIP_DECORATE, classBlocks, judgeClassBlocks } from "../scenarios/rarefields.mjs";
import { parseUdmf, readPk3 } from "../src/udmf.mjs";
import { PCD } from "../src/acs.mjs";

test("the map holds two parents, a child and an heir, each with its tid", () => {
  const things = parseUdmf(buildRareFieldsMap().textmap()).thing.filter((t) => t.type !== 1 && t.type < GROWN.firstEd);
  assert.deepEqual(things.map((t) => [t.type, t.id]), [[ED[PARENT], TID.changed], [ED[PARENT], TID.sibling], [ED[CHILD], TID.child], [ED[HEIR], TID.heir]]);
});

test("the child sets its own accuracy and the heir nothing", () => {
  assert.match(DECORATE, new RegExp(`actor ${CHILD} : ${PARENT} ${ED[CHILD]}\\n\\{\\n  Accuracy 44\\n\\}`));
  assert.match(DECORATE, new RegExp(`actor ${HEIR} : ${PARENT} ${ED[HEIR]}\\n\\{\\n\\}`));
  assert.match(DECORATE, /A_SetUserVar\("user_sum", accuracy \+ stamina\)/);
});

test("each grown parent holds its own variables and a child after it that sets its own accuracy", () => {
  assert.equal(GROWN_TIDS.length, GROWN.count);
  const parents = [...GROWN_DECORATE.matchAll(/actor (ZXRareGrown\d+) (\d+)\n\{([^}]*)/g)];
  assert.equal(parents.length, GROWN.count);
  parents.forEach(([, name, ed, body], i) => {
    assert.equal(name, grownName(i + 1));
    assert.equal(Number(ed), GROWN.firstEd + i);
    assert.equal((body.match(/var int user_v/g) || []).length, (i % 16) + 1);
    assert.ok(GROWN_DECORATE.includes(`actor ${grownName(i + 1, true)} : ${name}\n{\n  Accuracy 44\n}`), name);
  });
  const placed = parseUdmf(buildRareFieldsMap().textmap()).thing.filter((t) => t.type >= GROWN.firstEd);
  assert.deepEqual(placed.map((t) => t.id), GROWN_TIDS);
  assert.ok(placed.every((t) => t.x > 16 && t.x < 752 && t.y > 16 && t.y < 496));
});

test("the grown parents' accuracies are summed in one check", () => {
  assert.deepEqual(READ.accuracySum([7, 8]), [PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 7, PCD.PUSHNUMBER, 33, PCD.GETACTORPROPERTY, PCD.ADD, PCD.PUSHNUMBER, 8, PCD.PUSHNUMBER, 33, PCD.GETACTORPROPERTY, PCD.ADD]);
  assert.deepEqual(CHECKS.at(-1).slice(1), ["accuracySum", GROWN_TIDS, GROWN.count * 11]);
});

test("each check reads a property, the user variable, a field through a handle, or a tid count", () => {
  assert.deepEqual(READ.prop([TID.heir, 33]), [PCD.PUSHNUMBER, TID.heir, PCD.PUSHNUMBER, 33, PCD.GETACTORPROPERTY]);
  assert.deepEqual(READ.field([TID.heir, S.accuracy]), [PCD.PUSHNUMBER, TID.heir, PCD.CALLFUNC, 1, 24020, PCD.PUSHNUMBER, S.accuracy, PCD.CALLFUNC, 2, 24030]);
  assert.ok(CHECKS.every(([, kind]) => READ[kind]));
  // [Doruk-Aksoy] One bit a check above the ran bit, all inside an int.
  assert.ok(CHECKS.length < 30);
});

test("a run's counter names each failed check with when it ran", () => {
  assert.deepEqual(judgeCounter("after the load", 1), []);
  assert.deepEqual(judgeCounter("after the load", null), ["the after the load checks never ran"]);
  assert.deepEqual(judgeCounter("before the save", 0), ["the before the save checks never ran"]);
  assert.deepEqual(judgeCounter("before the save", 1 + 2 ** 7), ["before the save: the sibling's accuracy"]);
});

test("the pk3 carries the map, its scripts, the statues and the counter", () => {
  const names = readPk3(buildRareFieldsPk3()).map((e) => e.name);
  for (const name of ["maps/MAP01.wad", "decorate.txt", "cvarinfo.txt"]) assert.ok(names.includes(name), name);
});

test("the skip_super class copies AActor's defaults and writes no rare field", () => {
  assert.match(SKIP_DECORATE, /^actor ZXRareSkip\n\{\n  skip_super\n/);
  assert.doesNotMatch(SKIP_DECORATE, /Accuracy|Stamina|RipperLevel|Tag|Score/);
});

test("a class reading a block it does not own fails the run", () => {
  assert.deepEqual(classBlocks("x\nfua_dev_classblocks: owned 300 shared 2 none 10\n"), { owned: 300, shared: 2, none: 10, names: [] });
  assert.deepEqual(classBlocks("  ZXRareSkip reads a block it does not own\nfua_dev_classblocks: owned 300 shared 1 none 10\n").names, ["ZXRareSkip"]);
  assert.deepEqual(judgeClassBlocks({ owned: 300, shared: 1, none: 10, names: ["ZXRareSkip"] }), ["1 class(es) read a rare-field block they do not own: ZXRareSkip"]);
  assert.equal(classBlocks("nothing"), null);
  assert.deepEqual(judgeClassBlocks({ owned: 300, shared: 0, none: 10 }), []);
  assert.deepEqual(judgeClassBlocks({ owned: 300, shared: 2, none: 10 }), ["2 class(es) read a rare-field block they do not own"]);
  assert.deepEqual(judgeClassBlocks(null), ["fua_dev_classblocks printed nothing"]);
});
