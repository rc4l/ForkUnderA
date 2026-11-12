import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeBanfile, BAN_FILES, SIDES } from "../scenarios/cmdlinebanfile.mjs";

test("sv_banfile passes naming both files and fails cut at the semicolon", () => {
  assert.deepEqual(judgeBanfile(`"sv_banfile" is "${BAN_FILES}"`), []);
  assert.deepEqual(judgeBanfile('"sv_banfile" is "zxa.txt"'), [`sv_banfile is "zxa.txt", not "${BAN_FILES}"`]);
  assert.match(judgeBanfile("Unknown command")[0], /no value/);
});

test("the side launches with the two files in one cvar", () => {
  assert.equal(SIDES[0].launchCvars.sv_banfile, BAN_FILES);
  assert.ok(BAN_FILES.includes(";"));
});
