import { test } from "node:test";
import assert from "node:assert/strict";
import { CHECKS, judgeFields, buildScript, buildAcsFieldsPk3, FN, FAR_X } from "../scenarios/acsfields.mjs";
import { PCD } from "../src/acs.mjs";

test("the log passes with every check's answer and names each wrong one", () => {
  assert.deepEqual(judgeFields(`f1 ${CHECKS.map((c) => c.want).join(" ")}`), []);
  const wrong = CHECKS.map((c, i) => (i === 0 ? 99 : c.want));
  assert.deepEqual(judgeFields(`f1 ${wrong.join(" ")}`), ["health read 99, not 20"]);
  assert.match(judgeFields("Unknown function").join(), /script 1 logged nothing/);
});

test("the far barrel's x is the truncated value GetActorX gives", () => {
  const far = CHECKS.find((c) => c.what === "the far barrel's x");
  assert.equal(far.want, (FAR_X * 65536) | 0);
  assert.ok(far.want < 0);
});

test("the script takes both handles before it prints, and the pk3 carries the DECORATE barrel", () => {
  const words = buildScript();
  assert.ok(words.indexOf(PCD.BEGINPRINT) > words.lastIndexOf(FN.GetActorHandle));
  assert.equal(FN.GetField, 24030);
  const text = buildAcsFieldsPk3().toString("latin1");
  assert.ok(text.includes("decorate.txt") && text.includes("user_list[3]"));
});
