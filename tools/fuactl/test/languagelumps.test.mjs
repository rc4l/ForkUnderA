// [Doruk-Aksoy] The LANGUAGE lumps template: what its wad holds and how its judge reads print's answers.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readWad } from "../src/udmf.mjs";
import { buildLanguageLumpsWad, judgeLanguageLumpsFrom, STEPS } from "../scenarios/languagelumps.mjs";

test("the wad holds a text LANGUAGE lump, a CSV one and LMACROS beside its map", () => {
  const lumps = readWad(buildLanguageLumpsWad());
  const languages = lumps.filter((l) => l.name === "LANGUAGE").map((l) => l.data.toString("latin1"));
  assert.equal(languages.length, 2);
  assert.match(languages[0], /^\[enu default\]/);
  assert.match(languages[0], /ZX_LOOPA = "\$\$ZX_LOOPB";/);
  assert.match(languages[1], /^identifier,default,fr\n/);
  assert.ok(lumps.some((l) => l.name === "LMACROS"));
  assert.ok(lumps.some((l) => l.name === "MAP01"));
});

test("the judge passes print's expected answers and names each wrong one", () => {
  const right = STEPS.map((s) => Object.fromEntries(Object.entries(s.want).map(([k, v]) => [k, `${v}\n`])));
  assert.deepEqual(judgeLanguageLumpsFrom("MAP01 vk", right), { line: "PASS MAP01 vk: aliases, escapes, macros, CSV and default text all as written", failed: 0 });

  const wrong = structuredClone(right);
  wrong[0].ZX_LOOPA = "$$ZX_LOOPB";
  delete wrong[3].TXT_YES;
  const r = judgeLanguageLumpsFrom("MAP01 vk", wrong);
  assert.equal(r.failed, 1);
  assert.match(r.line, /ZX_LOOPA printed "\$\$ZX_LOOPB", not "ZX_LOOPA unknown"/);
  assert.match(r.line, /TXT_YES printed "", not "Mod yes"/);
});
