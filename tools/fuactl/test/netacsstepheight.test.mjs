import { test } from "node:test";
import assert from "node:assert/strict";
import { judgeHeights, setScript, showScript, reshowScript, buildNetStepHeightPk3, WANT, DEFAULT, SHOW_SCRIPT } from "../scenarios/netacsstepheight.mjs";
import { ACSF, PCD } from "../src/acs.mjs";
import { SPECIAL } from "../src/doomnums.mjs";

test("every machine must hold both new heights", () => {
  assert.deepEqual(judgeHeights({ server: WANT, client: WANT }), []);
  assert.deepEqual(judgeHeights({ client: { step: DEFAULT, drop: WANT.drop } }), ["client holds a step height of 24, not 40"]);
  assert.deepEqual(judgeHeights({ joiner: { step: WANT.step, drop: DEFAULT } }), ["joiner holds a drop-off height of 24, not 100"]);
});

test("the server sets both heights and has the clients show theirs", () => {
  const words = setScript();
  assert.ok(words.includes(WANT.step) && words.includes(WANT.drop));
  const exec = [PCD.PUSHNUMBER, SHOW_SCRIPT, PCD.PUSHNUMBER, 0, PCD.LSPEC1 + 1, SPECIAL.ACS_ExecuteAlways];
  for (const script of [setScript(), reshowScript()]) assert.ok(script.join().includes(exec.join()));
});

test("the clientside script writes the client's own user cvars", () => {
  const words = showScript();
  assert.equal(words.filter((w, i) => w === ACSF.SetUserCVar && words[i - 2] === PCD.CALLFUNC).length, 2);
});

test("the pk3 declares the user and server cvars", () => {
  const text = buildNetStepHeightPk3().toString("latin1");
  assert.ok(text.includes("user int zx_step") && text.includes("server int zx_drop_server"));
});
