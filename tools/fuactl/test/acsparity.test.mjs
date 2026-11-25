import { test } from "node:test";
import assert from "node:assert/strict";
import { CHECKS, judgeParity, judgeNullDamage, parseParity, lineCoord, buildScript, buildAcsParityPk3, TERRAIN } from "../scenarios/acsparity.mjs";
import { ACSF, PCD } from "../src/acs.mjs";

const logOf = (values) => `parity ${values.map((v) => (typeof v === "string" ? `<${v}>` : v)).join(" ")}\n`;

test("the log passes with every check's answer and names each wrong one", () => {
  assert.deepEqual(judgeParity(logOf(CHECKS.map((c) => c.want))), []);
  const wrong = CHECKS.map((c) => (c.str ? "x" : 12345));
  assert.equal(judgeParity(logOf(wrong)).length, CHECKS.length);
  assert.match(judgeParity(logOf(wrong))[0], /Floor\(1\.5\) is 12345, not 65536/);
  assert.deepEqual(judgeParity("Unknown function 207"), ["script 1 logged no checks (Unknown function 207)"]);
  assert.deepEqual(judgeParity("parity 1 2"), [`script 1 logged 2 values, not ${CHECKS.length}`]);
});

test("UZDoom is judged by its own Ceil, which adds one to a whole number", () => {
  const uz = CHECKS.map((c) => ("uzdoom" in c ? c.uzdoom : c.want));
  assert.deepEqual(judgeParity(logOf(uz), "uzdoom"), []);
  assert.match(judgeParity(logOf(uz)).join(), /Ceil\(2\.0\) is 196608, not 131072/);
});

test("strings parse without their brackets, an empty one too", () => {
  assert.deepEqual(parseParity("parity 1 <Solid> <> -2"), [1, "Solid", "", -2]);
  assert.equal(parseParity("nothing here"), null);
});

test("DamageActor on a missing tid must give -1", () => {
  assert.deepEqual(judgeNullDamage("damagenull -1"), []);
  assert.deepEqual(judgeNullDamage("damagenull 0"), ["DamageActor on a missing tid gave 0, not -1"]);
  assert.match(judgeNullDamage("").join(), /script 2 logged nothing/);
});

test("line points follow UZDoom's normal and round half to even", () => {
  assert.equal(lineCoord(0, [512, 0], [0, 0], 16384, 0), 384 * 65536);
  assert.equal(lineCoord(1, [512, 0], [0, 0], 32768, 16 * 65536), 16 * 65536);
  assert.equal(lineCoord(0, [0, 0], [0, 0], 32768, 65536), 0);
  assert.equal(lineCoord(0, [0, 0], [1, 0], 5, 0), 5);
  assert.equal(lineCoord(0, [0, 0], [0.5, 0], 5, 0), 2);
  assert.equal(lineCoord(0, [0, 0], [0.5, 0], 3, 0), 2);
});

test("the script runs the named script through StrArg before it prints", () => {
  const words = buildScript();
  assert.deepEqual(words.slice(0, 5), [PCD.PUSHNUMBER, 4, PCD.CALLFUNC, 1, ACSF.StrArg]);
  assert.ok(words.indexOf(PCD.BEGINPRINT) > words.indexOf(PCD.DELAYDIRECT));
});

test("the pk3 holds the map, its named script and the terrain", () => {
  const text = buildAcsParityPk3().toString("latin1");
  assert.ok(text.includes("maps/MAP01.wad") && text.includes("terrain.txt") && text.includes("zxstrarg"));
  assert.match(TERRAIN, /floor CEIL3_5 ZxMud/);
});
