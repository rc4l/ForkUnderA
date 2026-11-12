import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeChecks, buildAcsCheckScriptPk3, buildScript, QUESTIONS, NAMED } from "../scenarios/acscheckscript.mjs";
import { ACSF, PCD } from "../src/acs.mjs";

test("the log passes with each question's answer and names each wrong one", () => {
  assert.deepEqual(judgeChecks(`checks ${QUESTIONS.map((q) => q.want).join(" ")}\n`), []);
  const wrong = judgeChecks(`checks ${QUESTIONS.map(() => 0).join(" ")}`);
  assert.equal(wrong.length, QUESTIONS.filter((q) => q.want !== 0).length);
  assert.match(wrong[0], /CheckScript of number 5 is 0, not 1/);
  assert.deepEqual(judgeChecks("Unknown function 188"), ["script 1 logged no answers (Unknown function 188)"]);
});

test("the script asks CheckScript once a question, with that question's argument count", () => {
  const words = buildScript();
  const calls = words.flatMap((w, i) => (w === PCD.CALLFUNC && words[i + 2] === ACSF.CheckScript ? [words[i + 1]] : []));
  assert.deepEqual(calls, QUESTIONS.map((q) => q.args.length));
});

test("the pk3's map is ACSE, for its named script", () => {
  const text = buildAcsCheckScriptPk3().toString("latin1");
  assert.ok(text.includes("maps/MAP01.wad"));
  assert.ok(text.includes("SNAM") && text.includes(NAMED));
});
